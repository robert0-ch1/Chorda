/*
  ==============================================================================

    PluginProcessor.cpp

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/KarplusSound.h"

using namespace pluck;

namespace
{
    /** 20 ms: no zipper noise under automation, still feels immediate. */
    constexpr double gainSmoothingSeconds = 0.02;

    /** Smooths phase jumps when a synced LFO relocks to the song position,
        so the damper tap never moves in one step. */
    constexpr float lfoCornerSeconds = 0.004f;

    /** Output ceiling, +6 dBFS. */
    constexpr float safetyCeiling = 1.9953f;
    constexpr float limiterReleaseSeconds = 0.08f;

    /** Fixed room; only the send level is exposed. */
    constexpr float reverbRoomSize = 0.82f;
    constexpr float reverbDamping  = 0.45f;

    /** Wet level at full send, roughly matches the dry pluck (see tests). */
    constexpr float reverbWetGain  = 1.0f;

    /** Reverb tail. Comb feedback 0.7 + 0.28 * roomSize at ~30 ms gives
        T60 of about 3.3 s at this room size. */
    constexpr double reverbTailSeconds = 3.5;
}

//==============================================================================
ChordaAudioProcessor::ChordaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Chorda", createParameterLayout()),
      presetManager (apvts)
{
    raw.exciterTone      = rawParameter (ParamID::exciterTone);
    raw.exciterAttack    = rawParameter (ParamID::exciterAttack);
    raw.exciterPosition  = rawParameter (ParamID::exciterPosition);
    raw.stringDecay      = rawParameter (ParamID::stringDecay);
    raw.stringSustain    = rawParameter (ParamID::stringSustain);
    raw.stringRelease    = rawParameter (ParamID::stringRelease);
    raw.stringBrightness = rawParameter (ParamID::stringBrightness);
    raw.stringDamper     = rawParameter (ParamID::stringDamper);
    raw.stringDamperPressure = rawParameter (ParamID::stringDamperPressure);
    raw.stringSub        = rawParameter (ParamID::stringSub);
    raw.voiceMode        = rawParameter (ParamID::voiceMode);
    raw.pitchGlide       = rawParameter (ParamID::pitchGlide);
    positionLfo.amount = rawParameter (ParamID::lfoAmount);
    positionLfo.rate   = rawParameter (ParamID::lfoRate);
    positionLfo.sync   = rawParameter (ParamID::lfoSync);
    positionLfo.centre = rawParameter (ParamID::stringDamper);
    positionLfo.depth  = lfoPositionDepth;
    positionLfo.lowest = pickPositionMinimum;
    positionLfo.highest = positionMaximum;

    pressureLfo.amount = rawParameter (ParamID::lfoPressureAmount);
    pressureLfo.rate   = rawParameter (ParamID::lfoPressureRate);
    pressureLfo.sync   = rawParameter (ParamID::lfoPressureSync);
    pressureLfo.centre = rawParameter (ParamID::stringDamperPressure);
    pressureLfo.depth  = lfoPressureDepth;
    pressureLfo.lowest = 0.0f;
    pressureLfo.highest = 1.0f;
    raw.pitchOctave      = rawParameter (ParamID::pitchOctave);
    raw.outputDrive      = rawParameter (ParamID::outputDrive);
    raw.outputWidth      = rawParameter (ParamID::outputWidth);
    raw.outputGain       = rawParameter (ParamID::outputGain);
    raw.outputReverb     = rawParameter (ParamID::outputReverb);

    synth.addSound (new KarplusSound());

    for (int i = 0; i < numVoices; ++i)
    {
        auto* voice = new KarplusVoice();
        voices.push_back (voice);
        synth.addVoice (voice);   // synth takes ownership
    }

    synth.setNoteStealingEnabled (true);
}

ChordaAudioProcessor::~ChordaAudioProcessor() = default;

std::atomic<float>* ChordaAudioProcessor::rawParameter (const char* id) const
{
    auto* value = apvts.getRawParameterValue (id);
    jassert (value != nullptr);   // unknown parameter ID
    return value;
}

//==============================================================================
void ChordaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    synthBuffer.setSize (1, samplesPerBlock);

    oversampling.initProcessing ((size_t) samplesPerBlock);
    oversampling.reset();
    setLatencySamples (juce::roundToInt (oversampling.getLatencyInSamples()));
    driveDcIn1 = driveDcOut1 = 0.0f;
    driveDcCoeff = 1.0f - juce::MathConstants<float>::twoPi * 10.0f / ((float) sampleRate * 2.0f);
    driveSmoothed.reset (sampleRate, gainSmoothingSeconds);
    driveSmoothed.setCurrentAndTargetValue (raw.outputDrive->load());

    outputGainSmoothed.reset (sampleRate, gainSmoothingSeconds);
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw.outputGain->load(), -60.0f));

    widener.prepare (sampleRate, samplesPerBlock);

    limiterGain = 1.0f;
    limiterRelease = 1.0f - std::exp (-1.0f / (limiterReleaseSeconds * (float) sampleRate));

    lfoSmoothingCoeff = 1.0f - std::exp (-1.0f / (lfoCornerSeconds * (float) sampleRate));
    for (auto* lfo : { &positionLfo, &pressureLfo })
    {
        lfo->buffer.assign ((size_t) samplesPerBlock, 0.0f);
        lfo->phase = 0.0;
        lfo->smoothed = 0.0f;
        lfo->amountSmoothed.reset (sampleRate, gainSmoothingSeconds);
        lfo->amountSmoothed.setCurrentAndTargetValue (lfo->amount->load());
    }

    juce::Reverb::Parameters room;
    room.roomSize = reverbRoomSize;
    room.damping  = reverbDamping;
    room.width    = 1.0f;
    room.wetLevel = 1.0f / 3.0f;   // juce::Reverb scales wet by 3, so this is unity; send applied later
    room.dryLevel = 0.0f;
    reverb.setParameters (room);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    reverbBuffer.setSize (2, samplesPerBlock);
    reverbRunning = false;
    reverbSendSmoothed.reset (sampleRate, gainSmoothingSeconds);
    reverbSendSmoothed.setCurrentAndTargetValue (0.0f);
}

void ChordaAudioProcessor::releaseResources()
{
}

bool ChordaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

//==============================================================================
void ChordaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    // Merge the on-screen keyboard into the MIDI stream.
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // Mod wheel, block-accurate, drives brightness.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isControllerOfType (1))
            modWheel = (float) message.getControllerValue() / 127.0f;
        else if (message.isNoteOn())
            noteOnCount.fetch_add (1);
    }

    // Transport is needed for LFO tempo sync.
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* host = getPlayHead())
        position = host->getPosition();

    if (position)
        if (const auto bpm = position->getBpm())
            if (*bpm > 1.0)
                hostBpm.store (*bpm);

    positionLfo.active = renderLfo (positionLfo, numSamples, position);
    pressureLfo.active = renderLfo (pressureLfo, numSamples, position);
    updateVoiceParameters();

    // 1. Voices, summed to mono. Sample-accurate: the synth splits at MIDI events.
    synthBuffer.setSize (1, numSamples, false, false, true);   // allocates only if the host exceeds samplesPerBlock
    synthBuffer.clear();
    synth.renderNextBlock (synthBuffer, midiMessages, 0, numSamples);
    auto* mono = synthBuffer.getWritePointer (0);

    // Telemetry
    stringLevel.store (synthBuffer.getMagnitude (0, 0, numSamples));
    activeVoiceCount.store (synth.getActiveVoiceCount());

    // 2. Drive
    applyDrive (synthBuffer);

    // 3. Gain and width, into the host buffer
    applyGain (mono, numSamples);
    applyWidth (buffer, mono, numSamples);

    // 4. Reverb send
    applyReverb (buffer, numSamples);

    // 5. Safety limiter
    applySafetyLimiter (buffer, numSamples);
}

void ChordaAudioProcessor::applySafetyLimiter (juce::AudioBuffer<float>& output, int numSamples)
{
    // Non-finite output: mute the block and reset stateful stages so it cannot persist.
    const auto numChannels = output.getNumChannels();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* data = output.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (data[i]))
            {
                output.clear();
                reverb.reset();
                widener.prepare (getSampleRate(), juce::jmax (numSamples, synthBuffer.getNumSamples()));
                limiterGain = 1.0f;
                return;
            }
    }

    // Peak limiter, no lookahead: instant attack, ~80 ms release. Unity gain
    // below the ceiling; the hard clip catches the onset sample.
    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (output.getSample (ch, i)));

        const auto needed = peak * limiterGain > safetyCeiling ? safetyCeiling / peak : 1.0f;
        if (needed < limiterGain)
            limiterGain = needed;
        else if (limiterGain < 1.0f)
            limiterGain = juce::jmin (1.0f, limiterGain + (1.0f - limiterGain) * limiterRelease);

        if (limiterGain < 1.0f)
            for (int ch = 0; ch < numChannels; ++ch)
                output.setSample (ch, i, juce::jlimit (-safetyCeiling, safetyCeiling, output.getSample (ch, i) * limiterGain));
    }
}

void ChordaAudioProcessor::updateVoiceParameters()
{
    VoiceParameters vp;
    vp.exciterTone    = raw.exciterTone->load();
    vp.attackSeconds  = raw.exciterAttack->load() * 0.001f;
    vp.pickPosition   = raw.exciterPosition->load();
    vp.decaySeconds   = raw.stringDecay->load();
    vp.sustainLevel   = raw.stringSustain->load();
    vp.releaseSeconds = raw.stringRelease->load();
    vp.brightnessHz   = raw.stringBrightness->load();
    vp.damperPosition = raw.stringDamper->load();
    vp.damperPressure = raw.stringDamperPressure->load();
    vp.subLevel       = raw.stringSub->load();
    vp.transposeSemitones = semitonesForOctaveIndex (juce::roundToInt (raw.pitchOctave->load()));
    vp.modWheel       = modWheel;
    vp.glideSeconds   = raw.pitchGlide->load();
    vp.damperModulation   = positionLfo.active ? positionLfo.buffer.data() : nullptr;
    vp.pressureModulation = pressureLfo.active ? pressureLfo.buffer.data() : nullptr;

    for (auto* voice : voices)
        voice->setParameters (vp);

    const auto mode = juce::roundToInt (raw.voiceMode->load());
    synth.setMaxPolyphony (polyphonyForVoiceMode (mode));
    synth.setLegato (mode == voiceModeLegato);
    synth.setGlideEnabled (raw.pitchGlide->load() > 0.0015f);
}

void ChordaAudioProcessor::applyDrive (juce::AudioBuffer<float>& mono)
{
    // Biased tanh for asymmetric saturation (even and odd harmonics), at 2x to
    // reduce aliasing. A DC blocker removes the bias offset. Drive 0 bypasses.
    driveSmoothed.setTargetValue (raw.outputDrive->load());

    juce::dsp::AudioBlock<float> block (mono);
    auto up = oversampling.processSamplesUp (block);
    auto* data = up.getChannelPointer (0);
    const auto numUp = (int) up.getNumSamples();

    for (int i = 0; i < numUp; ++i)
    {
        const auto drive = (i & 1) == 0 ? driveSmoothed.getNextValue() : driveSmoothed.getCurrentValue();
        if (drive <= 0.0f)
            continue;

        const auto preGain = 1.0f + drive * drive * 29.0f;             // up to ~30 dB
        const auto bias    = 0.2f * drive;                             // asymmetry, even harmonics
        const auto makeup  = std::pow (preGain, -0.4f);                // roughly constant loudness
        const auto amount  = juce::jmin (1.0f, drive * 2.0f);          // wet blend over the first half

        const auto x = data[i];
        const auto shaped = (std::tanh (preGain * x + bias) - std::tanh (bias)) * makeup;
        const auto driven = x + amount * (shaped - x);

        const auto dcFree = driven - driveDcIn1 + driveDcCoeff * driveDcOut1;
        driveDcIn1  = driven;
        driveDcOut1 = dcFree;
        data[i] = dcFree;
    }

    oversampling.processSamplesDown (block);
}

void ChordaAudioProcessor::applyGain (float* mono, int numSamples)
{
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (raw.outputGain->load(), -60.0f));

    for (int i = 0; i < numSamples; ++i)
        mono[i] *= outputGainSmoothed.getNextValue();
}

void ChordaAudioProcessor::applyWidth (juce::AudioBuffer<float>& output, const float* mono, int numSamples)
{
    const auto numOutputs = output.getNumChannels();
    if (numOutputs == 0)
        return;

    if (numOutputs == 1)
    {
        // Mono output bypasses the widener.
        output.copyFrom (0, 0, mono, numSamples);
    }
    else
    {
        widener.process (mono, output.getWritePointer (0), output.getWritePointer (1), numSamples, raw.outputWidth->load());

        // Extra channels copy the left
        for (int ch = 2; ch < numOutputs; ++ch)
            output.copyFrom (ch, 0, output, 0, 0, numSamples);
    }

    outputPeak.store (output.getMagnitude (0, numSamples));
}

//==============================================================================
float ChordaAudioProcessor::lfoSyncedBeats (float rateHz) const
{
    // Nearest division at the current tempo, by log distance.
    const auto freeSeconds = 1.0f / juce::jmax (0.001f, rateHz);
    const auto beatSeconds = 60.0f / (float) juce::jmax (1.0, hostBpm.load());
    auto best = lfoDivisionBeats[0];
    auto bestDistance = 1.0e9f;

    for (const auto beats : lfoDivisionBeats)
    {
        const auto distance = std::abs (std::log (beats * beatSeconds / freeSeconds));
        if (distance < bestDistance) { bestDistance = distance; best = beats; }
    }

    return best;
}

juce::String ChordaAudioProcessor::getLfoRateText (bool pressure) const
{
    const auto& lfo = pressure ? pressureLfo : positionLfo;
    const auto rate = lfo.rate->load();
    if (lfo.sync->load() <= 0.5f)
        return juce::String (rate, rate < 10.0f ? 2 : 1) + " Hz";

    const auto beats = lfoSyncedBeats (rate);
    for (size_t i = 0; i < lfoDivisionBeats.size(); ++i)
        if (juce::exactlyEqual (lfoDivisionBeats[i], beats))
            return lfoDivisionNames[i];

    return {};
}

bool ChordaAudioProcessor::renderLfo (DamperLfo& lfo, int numSamples,
                                      const juce::Optional<juce::AudioPlayHead::PositionInfo>& position)
{
    if ((int) lfo.buffer.size() < numSamples)
        lfo.buffer.resize ((size_t) numSamples);   // allocates only if the host exceeds samplesPerBlock

    lfo.amountSmoothed.setTargetValue (lfo.amount->load());

    // Synced: rate from tempo; while playing, phase from PPQ so it is
    // repeatable per song position. Free: runs at the Rate value.
    const auto sr = getSampleRate();
    double cyclesPerSample = lfo.rate->load() / sr;

    if (lfo.sync->load() > 0.5f)
    {
        const auto beats = (double) lfoSyncedBeats (lfo.rate->load());
        cyclesPerSample = hostBpm.load() / 60.0 / beats / sr;

        if (position && position->getIsPlaying())
            if (const auto ppq = position->getPpqPosition())
                lfo.phase = *ppq / beats - std::floor (*ppq / beats);
    }

    // Idle: voices use the unmodulated parameter.
    if (! lfo.amountSmoothed.isSmoothing() && lfo.amountSmoothed.getTargetValue() <= 0.0f)
    {
        lfo.now.store (-1.0f);
        return false;
    }

    // Bipolar around the parameter value.
    const auto centre = lfo.centre->load();
    for (int i = 0; i < numSamples; ++i)
    {
        const auto sine = std::sin (juce::MathConstants<float>::twoPi * (float) lfo.phase);
        lfo.smoothed += (sine - lfo.smoothed) * lfoSmoothingCoeff;
        const auto swing = lfo.smoothed * lfo.amountSmoothed.getNextValue() * lfo.depth;
        lfo.buffer[(size_t) i] = juce::jlimit (lfo.lowest, lfo.highest, centre + swing);

        lfo.phase += cyclesPerSample;
        if (lfo.phase >= 1.0)
            lfo.phase -= std::floor (lfo.phase);
    }

    lfo.now.store (lfo.buffer[(size_t) numSamples - 1]);
    return true;
}

void ChordaAudioProcessor::applyReverb (juce::AudioBuffer<float>& output, int numSamples)
{
    const auto send = raw.outputReverb->load();
    reverbSendSmoothed.setTargetValue (send * reverbWetGain);

    // Send 0 after fade-out: bypass and reset, so re-enabling starts from an empty tail.
    if (send <= 0.0f && ! reverbSendSmoothed.isSmoothing())
    {
        if (reverbRunning)
        {
            reverb.reset();
            reverbRunning = false;
        }
        return;
    }
    reverbRunning = true;


    const auto numChannels = juce::jmin (2, output.getNumChannels());
    if (numChannels == 0)
        return;

    reverbBuffer.setSize (2, numSamples, false, false, true);
    for (int ch = 0; ch < numChannels; ++ch)
        reverbBuffer.copyFrom (ch, 0, output, ch, 0, numSamples);

    if (numChannels == 2)
        reverb.processStereo (reverbBuffer.getWritePointer (0), reverbBuffer.getWritePointer (1), numSamples);
    else
        reverb.processMono (reverbBuffer.getWritePointer (0), numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto wet = reverbSendSmoothed.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            output.addSample (ch, i, wet * reverbBuffer.getSample (ch, i));
    }

    // Extra channels copy the left
    for (int ch = 2; ch < output.getNumChannels(); ++ch)
        output.copyFrom (ch, 0, output, 0, 0, numSamples);

    outputPeak.store (output.getMagnitude (0, numSamples));
}

//==============================================================================
double ChordaAudioProcessor::getTailLengthSeconds() const
{
    // String release plus reverb tail.
    return (double) raw.stringRelease->load() + (raw.outputReverb->load() > 0.0f ? reverbTailSeconds : 0.0);
}

//==============================================================================
juce::AudioProcessorEditor* ChordaAudioProcessor::createEditor()
{
    return new ChordaAudioProcessorEditor (*this);
}

//==============================================================================
void ChordaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // APVTS state: parameters and current preset name.
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChordaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presetManager.stateRestored();
        }
    }
}

//==============================================================================
// Plugin wrapper entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordaAudioProcessor();
}
