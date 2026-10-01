/*
  ==============================================================================

    KarplusVoice.cpp

    See KarplusVoice.h for the block diagram. Everything in this file runs on
    the audio thread: no allocation, no locks, no logging.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "KarplusSound.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

KarplusVoice::KarplusVoice()
{
    // Give the buffers a sensible size straight away; the synthesiser will
    // call setCurrentPlaybackSampleRate() again with the real rate.
    setCurrentPlaybackSampleRate (44100.0);
}

void KarplusVoice::setCurrentPlaybackSampleRate (double newRate)
{
    juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

    if (newRate <= 0.0)
        return;

    const auto sr = (float) newRate;
    const auto capacity = (size_t) std::ceil (sr / lowestFrequencyHz) + 8;

    delayLine.assign (capacity, 0.0f);
    exciterHistory.assign (capacity, 0.0f);
    damperHistory.assign (capacity * (size_t) damperStages / 2 + 8, 0.0f);   // the longest tap: N x half a period
    listenHistory.assign (capacity * (size_t) (damperStages / 2 + 2) + 8, 0.0f); // a period, plus the reference behind both filters
    damperLevelCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (damperLevelSeconds * sr));
    damperGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (damperGlideSeconds * sr)));
    loopDelayGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (loopDelayGlideSeconds * sr)));

    // One-pole high-pass: R = 1 - 2*pi*fc/fs is accurate enough at 10 Hz.
    dcBlockerCoeff = 1.0f - twoPi * dcBlockerHz / sr;
    cutTail.assign ((size_t) juce::jmax (1, juce::roundToInt (cutFadeSeconds * sr)), 0.0f);
    scratchTail.assign (cutTail.size(), 0.0f);
    cutTailRead = cutTailLeft = 0;
    subLevelCoeff  = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (subLevelSeconds * sr));

    // A note that was sounding at the old rate has meaningless buffer contents now.
    if (stage != Stage::idle)
        endVoice();
}

void KarplusVoice::setParameters (const VoiceParameters& newParams) noexcept
{
    const bool stringChanged = ! juce::exactlyEqual (params.decaySeconds,   newParams.decaySeconds)
                            || ! juce::exactlyEqual (params.sustainLevel,   newParams.sustainLevel)
                            || ! juce::exactlyEqual (params.releaseSeconds, newParams.releaseSeconds)
                            || ! juce::exactlyEqual (params.brightnessHz,   newParams.brightnessHz)
                            || ! juce::exactlyEqual (params.damperPosition, newParams.damperPosition)
                            || ! juce::exactlyEqual (params.pickPosition,   newParams.pickPosition)
                            || ! juce::exactlyEqual (params.damperPressure, newParams.damperPressure)
                            || ! juce::exactlyEqual (params.modWheel,       newParams.modWheel)
                            || (params.damperModulation == nullptr) != (newParams.damperModulation == nullptr)
                            || (params.pressureModulation == nullptr) != (newParams.pressureModulation == nullptr)
                            || params.transposeSemitones != newParams.transposeSemitones;

    params = newParams;

    // Everything about the string can be tweaked while a note rings. The
    // exciter settings, in contrast, are only read at note-on.
    if (stage != Stage::idle && stringChanged)
    {
        updateTuning();
        updateFeedbackTarget();
    }

}

bool KarplusVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<KarplusSound*> (sound) != nullptr;
}

void KarplusVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    soundingNote = midiNoteNumber;   // legato slides this on without a new note-on
    holdBlend = 0.0f;
    holdUpdateCounter = 0;

    stage           = Stage::exciting;
    velocityGain    = velocity * voiceGainTrim;
    pitchBend       = (float) (currentPitchWheelPosition - 8192) / 8192.0f;

    // Velocity also decides how the string is plucked: hard is shorter and brighter.
    velocityBrightness = juce::jmap (velocity, velocityBrightnessSoft, velocityBrightnessHard);
    const auto attackFactor = juce::jmap (velocity, velocityAttackFactorSoft, velocityAttackFactorHard);

    // A fresh note must never hear what the previous one left in the loop.
    clearBuffers();
    damperDelay = 0.0f;           // a new note starts with the damper where it is, not gliding from the last one
    snapLoopDelay = true;         // and with its string the right length at once
    updateTuning();
    snapLoopDelay = false;
    glideDamper (true);

    // Exciter: energy is fed in for "attack" seconds under a raised-cosine
    // window. A few milliseconds is a pluck; hundreds of milliseconds is a bow.
    const auto sr = (float) getSampleRate();
    attackSamplesTotal = juce::jmax (2, juce::roundToInt (params.attackSeconds * attackFactor * sr));
    attackSamplesLeft  = attackSamplesTotal;
    exciterPhase       = 0.0f;
    exciterFinished    = false;

    prepareExcitation();

    // Damping starts at the "decay" rate and is smoothed over one period, so a
    // change of stage never steps the level.
    feedbackSmoothing = juce::jlimit (0.001f, 1.0f, 1.0f / periodSamples);
    updateFeedbackTarget();
    feedbackGain = feedbackTarget;

    periodCounter          = 0;
    periodEnergy           = 0.0f;
    lastPeriodRms          = 0.0f;
    periodsSinceExcitation = 0;
    referenceLevel         = 0.0f;
    stringIsSilent         = false;
}

void KarplusVoice::glideFromFrequency (float hz) noexcept
{
    glideFromFrequency (hz, params.glideSeconds);
}

void KarplusVoice::glideFromFrequency (float hz, float seconds) noexcept
{
    // Called just after the note starts: the string is retuned to where the
    // last note was and slides from there to this one.
    if (seconds <= 0.0015f || hz <= 0.0f || stage == Stage::idle)
        return;

    glideStartLog     = std::log2 (hz);
    glideTargetLog    = std::log2 (targetFrequencyHz);
    if (juce::exactlyEqual (glideStartLog, glideTargetLog))
        return;

    glideSamplesTotal = juce::jmax (1, juce::roundToInt (seconds * (float) getSampleRate()));
    glideSamplesLeft  = glideSamplesTotal;
    glideFrequencyHz  = hz;
    gliding           = true;

    updateTuning();
    updateFeedbackTarget();

    // The string is plucked at the pitch it starts from. Its pluck was set up
    // for the note it is heading to, which made a glide down up to 4 dB too
    // loud and a glide up up to 6 dB too quiet: set it up again here, while
    // nothing has been played yet.
    if (stage == Stage::exciting && attackSamplesLeft == attackSamplesTotal)
    {
        prepareExcitation();
        feedbackSmoothing = juce::jlimit (0.001f, 1.0f, 1.0f / periodSamples);
        feedbackGain = feedbackTarget;
    }
}

void KarplusVoice::slideToNote (int midiNoteNumber) noexcept
{
    // Legato: the string keeps ringing and is retuned to the new note, gliding
    // there over Glide, and never quicker than a click-free slide.
    if (stage == Stage::idle)
        return;

    const auto from = frequencyHz;
    soundingNote = midiNoteNumber;
    gliding = false;
    updateTuning();
    glideFromFrequency (from, juce::jmax (params.glideSeconds, legatoMinimumSlideSeconds));
    updateFeedbackTarget();
}

void KarplusVoice::advanceGlide (int samples) noexcept
{
    if (! gliding)
        return;

    glideSamplesLeft = juce::jmax (0, glideSamplesLeft - samples);

    const auto t = 1.0f - (float) glideSamplesLeft / (float) glideSamplesTotal;
    glideFrequencyHz = std::exp2 (glideStartLog + (glideTargetLog - glideStartLog) * t);

    if (glideSamplesLeft == 0)
    {
        gliding = false;
        glideFrequencyHz = targetFrequencyHz;
    }

    updateTuning();
    updateFeedbackTarget();
}

void KarplusVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff && stage != Stage::idle)
    {
        // Normal note-off: damp the string at the "release" rate. The voice
        // frees itself in renderNextBlock() once the string has died away.
        setStage (Stage::releasing);
    }
    else
    {
        // Voice stealing or an "all notes off": stop now, but fade out.
        captureCutTail();
        endVoice();
    }
}

void KarplusVoice::pitchWheelMoved (int newPitchWheelValue)
{
    pitchBend = (float) (newPitchWheelValue - 8192) / 8192.0f;

    if (stage != Stage::idle)
    {
        updateTuning();
        updateFeedbackTarget();
    }
}

void KarplusVoice::captureCutTail() noexcept
{
    if (stage == Stage::idle || cutTail.empty())
        return;

    // Play the string on for a few milliseconds, now, under a fade, and keep
    // that to be mixed in while the next note starts from a clean string.
    // Whatever is left of an earlier tail is folded in, so two quick cuts in
    // a row still fade.
    const auto length = (int) cutTail.size();
    for (int i = 0; i < length; ++i)
    {
        const auto earlier = i < cutTailLeft ? cutTail[(size_t) ((cutTailRead + i) % length)] : 0.0f;
        const auto fade = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i + 1) / (float) length);
        scratchTail[(size_t) i] = earlier;
        scratchTail[(size_t) i] += renderSample() * fade;
    }
    std::copy (scratchTail.begin(), scratchTail.begin() + length, cutTail.begin());
    cutTailRead = 0;
    cutTailLeft = length;
}

void KarplusVoice::mixCutTail (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept
{
    const auto length = (int) cutTail.size();
    for (int i = 0; i < numSamples && cutTailLeft > 0; ++i, --cutTailLeft)
    {
        const auto sample = cutTail[(size_t) cutTailRead];
        if (++cutTailRead >= length)
            cutTailRead = 0;
        for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
            outputBuffer.addSample (ch, startSample + i, sample);
    }
}

void KarplusVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (cutTailLeft > 0)
        mixCutTail (outputBuffer, startSample, numSamples);

    if (stage == Stage::idle)
        return;

    const auto numChannels = outputBuffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        // The LFOs move the finger along the string and press it harder and
        // softer, and the filter follows them every sample.
        if (params.damperModulation != nullptr || params.pressureModulation != nullptr)
        {
            if (params.damperModulation != nullptr)
                damperModulated = params.damperModulation[startSample + i];
            if (params.pressureModulation != nullptr)
                pressureModulated = params.pressureModulation[startSample + i];
            updateDamperFilter();
        }

        const auto sample = renderSample();
        for (int ch = 0; ch < numChannels; ++ch)
            outputBuffer.addSample (ch, startSample + i, sample);

        // Easing the loop filter into and out of the hold, in small steps.
        const auto holdTarget = stage == Stage::holding ? 1.0f : 0.0f;
        if (! juce::exactlyEqual (holdBlend, holdTarget) && ++holdUpdateCounter >= holdUpdateInterval)
        {
            holdUpdateCounter = 0;
            const auto step = (float) holdUpdateInterval / (holdBlendSeconds * (float) getSampleRate());
            holdBlend = holdTarget > holdBlend ? juce::jmin (holdTarget, holdBlend + step) : juce::jmax (holdTarget, holdBlend - step);
            updateTuning();
            updateFeedbackTarget();
        }

        // Retuning is far too heavy to do every sample, and a glide does not
        // need it: a thirty-second of a millisecond is finer than the ear.
        if (gliding && ++glideUpdateCounter >= glideUpdateInterval)
        {
            glideUpdateCounter = 0;
            advanceGlide (glideUpdateInterval);
        }
    }

    // Free the voice once the string has decayed to silence (decided per
    // period in periodCompleted()), whether that is the end of a release or a
    // short decay with the key still held. Freeing early keeps voices
    // available for new notes instead of hoarding them.
    if (stringIsSilent)
        endVoice();
}

float KarplusVoice::renderSample() noexcept
{
    const auto excitation = exciterFinished ? 0.0f : nextExciterSample();

    // Glide the loop length towards where the tuning wants it.
    if (! juce::exactlyEqual (loopDelaySamples, loopDelayTarget))
    {
        loopDelaySamples += (loopDelayTarget - loopDelaySamples) * loopDelayGlideCoeff;
        if (std::abs (loopDelayTarget - loopDelaySamples) < 1.0e-4f)
            loopDelaySamples = loopDelayTarget;
        updateDelayInterpolator();
    }

    // Read the string one period ago (fractional).
    const auto signal = readDelayLine();

    // Loop low-pass: high partials lose energy on every round trip, exactly
    // as they do in a real string. This is what "Brightness" controls, and it
    // stays in the loop at every stage: a string that stopped losing its highs
    // would ring on whatever noise it was given, for ever.
    auto smoothed = signal;
    for (int k = 0; k < loopStages; ++k)
    {
        auto& state = loopStageState[(size_t) k];
        state = (1.0f - loopStageCoeff) * smoothed + loopStageCoeff * state;
        smoothed = state;
    }
    const auto lowPassed = smoothed;

    // Loop high-pass: removes the DC mode so the feedback may exceed 1.
    const auto highPassed = lowPassed - highPassIn1 + highPassCoeff * highPassOut1;
    highPassIn1  = lowPassed;
    highPassOut1 = highPassed;

    // The damper is not in the loop any more: the string rings exactly as
    // the envelope says, and the damper shapes what you hear of it (below).
    const auto filtered = highPassed;

    // Damping: glide towards the current stage's feedback gain.
    feedbackGain += (feedbackTarget - feedbackGain) * feedbackSmoothing;

    // Never above the stability cap, even while gliding towards a lower
    // target: after a retune (a bend, a glide, an octave jump) the cap can
    // drop at once while the smoothed gain is still up at the old value, and
    // a loop gain over 1 for a few periods is all a runaway needs.
    feedbackGain = juce::jmin (feedbackGain, feedbackCap);

    // Feed the string back into itself plus any new excitation.
    const auto string = excitation + feedbackGain * filtered;
    delayLine[(size_t) writeIndex] = string;

    if (++writeIndex >= (int) delayLine.size())
        writeIndex = 0;

    // Accumulate energy over each period; stage decisions happen once per period.
    periodEnergy += filtered * filtered;

    if (++periodCounter >= (int) periodSamples)
    {
        lastPeriodRms = std::sqrt (periodEnergy / (float) periodCounter);
        periodEnergy  = 0.0f;
        periodCounter = 0;
        periodCompleted();
    }

    // Sub: a sine an octave below. Its period is exactly twice the string's
    // own (not twice the nominal f0, which the loop misses by a fraction of a
    // sample), or the two would drift apart and phase against each other. Its
    // level comes from a continuous mean square, not the once-a-period RMS,
    // whose measuring window slides and would modulate the sub.
    // The string is heard where the pluck goes in, not at the far end of the
    // delay line: read there, every note waited one whole period before it
    // sounded (31 ms at C1, 120 ms at C-1), which played as latency.
    glideDamper (false);
    // (The pluck plus the returning wave, before the feedback gain, which
    // only makes up the loop's own losses: the level is what it always was.)
    auto output = applyDamper (excitation + filtered);
    if (params.subLevel > 0.0f)
    {
        subMeanSquare += (filtered * filtered - subMeanSquare) * subLevelCoeff;
        output += std::sin (twoPi * subPhase) * std::sqrt (subMeanSquare) * subGain * params.subLevel;

        subPhase += subPhaseStep;
        if (subPhase >= 1.0f)
            subPhase -= 1.0f;
    }

    // DC blocker (first-order high-pass), then velocity.
    const auto dcFree = output - dcIn1 + dcBlockerCoeff * dcOut1;
    dcIn1  = output;
    dcOut1 = dcFree;

    // Last line of defence: a voice that has run away is silenced and freed
    // rather than allowed to reach the output. A string that is working never
    // comes near this.
    if (! std::isfinite (dcFree) || std::abs (dcFree) > runawayLevel)
    {
        clearBuffers();
        stringIsSilent = true;
        return 0.0f;
    }

    return dcFree * velocityGain;
}

void KarplusVoice::periodCompleted() noexcept
{
    if (! exciterFinished)
        return;

    // The excitation reaches the read tap one period after it is written, so
    // the first completed period after the exciter stops may still be partly
    // empty. Only from the second one on is lastPeriodRms the real level.
    ++periodsSinceExcitation;
    if (periodsSinceExcitation < 2)
        return;

    if (stage == Stage::exciting)
    {
        // The pluck (or bow stroke) is over: from here on the string only loses energy.
        referenceLevel = lastPeriodRms;
        setStage (Stage::decaying);
    }
    else if (stage == Stage::decaying && params.sustainLevel > 0.0f
             && periodsSinceExcitation >= minimumPeriodsBeforeHold)
    {
        // Sustain: once the string has dropped to the hold level, stop damping
        // it. Never before it has settled, though, however high the sustain:
        // the loop stops damping when it holds, so whatever is still ringing
        // at that moment rings for ever.
        const auto holdDb    = -sustainRangeDb * (1.0f - params.sustainLevel);
        const auto holdLevel = referenceLevel * juce::Decibels::decibelsToGain (holdDb);

        if (lastPeriodRms <= holdLevel)
            setStage (Stage::holding);
    }

    if (lastPeriodRms < silenceThreshold)
        stringIsSilent = true;
}

void KarplusVoice::setStage (Stage newStage) noexcept
{
    stage = newStage;

    // Holding raises the feedback to the cap, which is the loop's own loss at
    // f0 made up exactly, so the fundamental holds; renderNextBlock eases the
    // low-pass to its light holding setting. Taking the low-pass out
    // altogether (as this once did) froze whatever top end the string still
    // had into a loop that no longer lost anything: on low notes a corner in
    // the waveform once a period, a quiet tick at the note's own rate
    // (measured on E2 at full sustain: a spike every 12 ms). Doing it all at
    // once also moved the loop delay, and that retune clicked.
    updateFeedbackTarget();
}

void KarplusVoice::clearBuffers() noexcept
{
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    std::fill (exciterHistory.begin(), exciterHistory.end(), 0.0f);
    std::fill (damperHistory.begin(), damperHistory.end(), 0.0f);
    std::fill (listenHistory.begin(), listenHistory.end(), 0.0f);
    damperLevelIn = damperLevelOut = 0.0f;
    damperMakeup = damperMakeupTarget = 1.0f;
    damperMakeupCounter = 0;
    subPhase            = 0.0f;
    subMeanSquare       = 0.0f;
    exciterStageState.fill (0.0f);

    writeIndex        = 0;
    exciterWriteIndex = 0;
    damperWriteIndex  = 0;
    loopStageState.fill (0.0f);
    highPassIn1 = highPassOut1 = 0.0f;
    allpassX1 = allpassY1 = 0.0f;
    dcIn1 = dcOut1    = 0.0f;
}

void KarplusVoice::endVoice() noexcept
{
    stage = Stage::idle;
    exciterFinished = true;
    clearCurrentNote();
}

} // namespace pluck
