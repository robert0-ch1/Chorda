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
    /** Master gain smoothing time. Long enough to avoid zipper noise when the
        knob is automated, short enough to feel immediate. */
    constexpr double gainSmoothingSeconds = 0.02;

    /** The sine itself is smooth, but its phase jumps when a synced LFO
        locks to a new song position. Moving the damper tap in one jump would
        click, so the output is rounded over about this long. */
    constexpr float lfoCornerSeconds = 0.004f;

    /** Nothing leaves the plugin louder than this: +6 dBFS. */
    constexpr float safetyCeiling = 1.9953f;
    constexpr float limiterReleaseSeconds = 0.08f;

    /** The room. Fixed: the one knob is how much is sent into it. */
    constexpr float reverbRoomSize = 0.82f;
    constexpr float reverbDamping  = 0.45f;

    /** The room at full send, on its own, is about as loud as the dry
        string, measured on a pluck (see the tests). */
    constexpr float reverbWetGain  = 1.0f;

    /** How long the room rings on after the strings stop. juce::Reverb's
        combs feed back 0.7 + 0.28 x room size over about 30 ms, which is
        3.3 s to fall by 60 dB at this size. */
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
        synth.addVoice (voice);   // the synthesiser takes ownership
    }

    // When all allowed voices are busy, the synthesiser re-uses one rather
    // than dropping the note.
    synth.setNoteStealingEnabled (true);
}

ChordaAudioProcessor::~ChordaAudioProcessor() = default;

std::atomic<float>* ChordaAudioProcessor::rawParameter (const char* id) const
{
    auto* value = apvts.getRawParameterValue (id);
    jassert (value != nullptr);   // a typo in Parameters.h would trip this
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
    room.wetLevel = 1.0f / 3.0f;   // juce::Reverb scales wet by 3: this is unity, and the send happens here
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
    // Merge notes played on the editor's on-screen keyboard into the MIDI stream.
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // The mod wheel is read here (block-accurate) and handed to the voices as
    // a brightness modulation.
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isControllerOfType (1))
            modWheel = (float) message.getControllerValue() / 127.0f;
        else if (message.isNoteOn())
            noteOnCount.fetch_add (1);
    }

    // The LFO can follow the host's tempo and bar, so the transport is read first.
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

    // 1. All voices summed to mono. The synthesiser splits the block at every
    //    MIDI event, so note timing is sample-accurate.
    synthBuffer.setSize (1, numSamples, false, false, true);   // no-op unless the host exceeds samplesPerBlock
    synthBuffer.clear();
    synth.renderNextBlock (synthBuffer, midiMessages, 0, numSamples);
    auto* mono = synthBuffer.getWritePointer (0);

    // Telemetry for the editor's string animation
    stringLevel.store (synthBuffer.getMagnitude (0, 0, numSamples));
    activeVoiceCount.store (synth.getActiveVoiceCount());

    // 2. Valve drive
    applyDrive (synthBuffer);

    // 3. Master gain, then the stereo width, out to the host's channels
    applyGain (mono, numSamples);
    applyWidth (buffer, mono, numSamples);

    // 4. The room
    applyReverb (buffer, numSamples);

    // 5. And a ceiling, for safety
    applySafetyLimiter (buffer, numSamples);
}

void ChordaAudioProcessor::applySafetyLimiter (juce::AudioBuffer<float>& output, int numSamples)
{
    // Anything that is not a number means something upstream has broken.
    // Drop the block and clear what could be carrying it on, rather than
    // hand it to the host.
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

    // A peak limiter with no lookahead: it clamps at once when a sample
    // would pass the ceiling and lets go over about 80 ms. Below the ceiling
    // its gain is exactly 1, so it leaves every normal signal bit for bit.
    // The hard clip after it catches the one sample it reacts on.
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
    // Valve-style saturation. A single-ended triode stage clips softly and
    // asymmetrically, which adds even as well as odd harmonics: tanh with a
    // bias does the same. Run at 2x so the harmonics it creates above Nyquist
    // fold back less, then a DC blocker removes the offset the asymmetry
    // leaves behind. At zero drive the stage is a wire.
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

        const auto preGain = 1.0f + drive * drive * 29.0f;             // up to ~30 dB into the valve
        const auto bias    = 0.2f * drive;                             // asymmetry: even harmonics
        const auto makeup  = std::pow (preGain, -0.4f);                // level roughly constant across the knob
        const auto amount  = juce::jmin (1.0f, drive * 2.0f);          // blend in over the first half of the knob

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
        // A mono host gets the string as it is; the widener is stereo-only.
        output.copyFrom (0, 0, mono, numSamples);
    }
    else
    {
        widener.process (mono, output.getWritePointer (0), output.getWritePointer (1), numSamples, raw.outputWidth->load());

        // Any further channels mirror the left
        for (int ch = 2; ch < numOutputs; ++ch)
            output.copyFrom (ch, 0, output, 0, 0, numSamples);
    }

    outputPeak.store (output.getMagnitude (0, numSamples));
}

//==============================================================================
float ChordaAudioProcessor::lfoSyncedBeats (float rateHz) const
{
    // Snap to whichever note division the knob is nearest at the current
    // tempo, measured in pitch rather than in seconds so the whole knob is usable.
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
        lfo.buffer.resize ((size_t) numSamples);   // only if the host exceeds samplesPerBlock

    lfo.amountSmoothed.setTargetValue (lfo.amount->load());

    // Synced, the LFO takes its rate from the tempo and, while the transport
    // runs, its phase from the song position, so it lands on the bar every
    // time the song is played. Free, it simply runs.
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

    // Idle: the voices are told to leave the damper where it is set.
    if (! lfo.amountSmoothed.isSmoothing() && lfo.amountSmoothed.getTargetValue() <= 0.0f)
    {
        lfo.now.store (-1.0f);
        return false;
    }

    // Bipolar, around where the parameter is set: the marker on the string
    // for the position, its height for the pressure.
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

    // At zero, and once the send has faded out, the room is left out
    // altogether: the signal passes bit for bit, and a room brought back in
    // later starts empty rather than with what was in it before.
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

    // Any further channels mirror the left, as the width stage does
    for (int ch = 2; ch < output.getNumChannels(); ++ch)
        output.copyFrom (ch, 0, output, 0, 0, numSamples);

    outputPeak.store (output.getMagnitude (0, numSamples));
}

//==============================================================================
double ChordaAudioProcessor::getTailLengthSeconds() const
{
    // After the last note-off the strings are damped at the release rate,
    // and the room rings on after them.
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
    // The APVTS tree holds every parameter plus the current preset name.
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
// This is the entry point the plugin wrappers use to create the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordaAudioProcessor();
}
