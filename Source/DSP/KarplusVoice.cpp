/*
  ==============================================================================

    KarplusVoice.cpp

    Note lifecycle and per-sample rendering. Everything except
    setCurrentPlaybackSampleRate() is real-time safe: no allocation, no locks.

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
    // Provisional size; the synthesiser sets the real rate later.
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
    damperHistory.assign (capacity * (size_t) damperStages / 2 + 8, 0.0f);   // longest tap: N x half a period
    listenHistory.assign (capacity * (size_t) (damperStages / 2 + 2) + 8, 0.0f); // a period plus the make-up reference delay
    damperLevelCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (damperLevelSeconds * sr));
    damperGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (damperGlideSeconds * sr)));
    loopDelayGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (loopDelayGlideSeconds * sr)));

    // R = 1 - 2 pi fc / fs, accurate enough at 10 Hz.
    dcBlockerCoeff = 1.0f - twoPi * dcBlockerHz / sr;
    cutTail.assign ((size_t) juce::jmax (1, juce::roundToInt (cutFadeSeconds * sr)), 0.0f);
    scratchTail.assign (cutTail.size(), 0.0f);
    cutTailRead = cutTailLeft = 0;
    subLevelCoeff  = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (subLevelSeconds * sr));

    // Buffer contents are invalid at the new rate.
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

    // String parameters apply to sounding notes; exciter parameters only at note-on.
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
    soundingNote = midiNoteNumber;   // changed by legato without a note-on
    holdBlend = 0.0f;
    holdUpdateCounter = 0;

    stage           = Stage::exciting;
    velocityGain    = velocity * voiceGainTrim;
    pitchBend       = (float) (currentPitchWheelPosition - 8192) / 8192.0f;

    // Harder velocity gives a shorter, brighter pluck.
    velocityBrightness = juce::jmap (velocity, velocityBrightnessSoft, velocityBrightnessHard);
    const auto attackFactor = juce::jmap (velocity, velocityAttackFactorSoft, velocityAttackFactorHard);

    clearBuffers();
    damperDelay = 0.0f;           // no damper glide from the previous note
    snapLoopDelay = true;         // no loop length glide either
    updateTuning();
    snapLoopDelay = false;
    glideDamper (true);

    // Excitation under a raised-cosine window: ms for a pluck, hundreds of ms for a bow.
    const auto sr = (float) getSampleRate();
    attackSamplesTotal = juce::jmax (2, juce::roundToInt (params.attackSeconds * attackFactor * sr));
    attackSamplesLeft  = attackSamplesTotal;
    exciterPhase       = 0.0f;
    exciterFinished    = false;

    prepareExcitation();

    // Feedback is smoothed with a one-period time constant so stage changes do not step.
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
    // Called right after startNote(): retune to `hz` and glide to the target.
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

    // The exciter depends on pitch: redo it for the start pitch if nothing has played yet.
    if (stage == Stage::exciting && attackSamplesLeft == attackSamplesTotal)
    {
        prepareExcitation();
        feedbackSmoothing = juce::jlimit (0.001f, 1.0f, 1.0f / periodSamples);
        feedbackGain = feedbackTarget;
    }
}

void KarplusVoice::slideToNote (int midiNoteNumber) noexcept
{
    // Keep the string ringing and glide to the new note, no faster than legatoMinimumSlideSeconds.
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
        // Damp at the release rate; renderNextBlock() frees the voice when silent.
        setStage (Stage::releasing);
    }
    else
    {
        // Stolen or all-notes-off: stop now, with a short fade.
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

    // Render cutFadeSeconds of the string under a fade, to be mixed under the
    // next note. Any remaining earlier tail is summed in.
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
        // LFO-driven damper position and pressure, updated per sample.
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

        // Blend the loop low-pass towards or away from the hold setting.
        const auto holdTarget = stage == Stage::holding ? 1.0f : 0.0f;
        if (! juce::exactlyEqual (holdBlend, holdTarget) && ++holdUpdateCounter >= holdUpdateInterval)
        {
            holdUpdateCounter = 0;
            const auto step = (float) holdUpdateInterval / (holdBlendSeconds * (float) getSampleRate());
            holdBlend = holdTarget > holdBlend ? juce::jmin (holdTarget, holdBlend + step) : juce::jmax (holdTarget, holdBlend - step);
            updateTuning();
            updateFeedbackTarget();
        }

        // Retuning is expensive; every glideUpdateInterval samples is enough.
        if (gliding && ++glideUpdateCounter >= glideUpdateInterval)
        {
            glideUpdateCounter = 0;
            advanceGlide (glideUpdateInterval);
        }
    }

    // Free the voice once silent, also while the key is still held.
    if (stringIsSilent)
        endVoice();
}

float KarplusVoice::renderSample() noexcept
{
    const auto excitation = exciterFinished ? 0.0f : nextExciterSample();

    if (! juce::exactlyEqual (loopDelaySamples, loopDelayTarget))
    {
        loopDelaySamples += (loopDelayTarget - loopDelaySamples) * loopDelayGlideCoeff;
        if (std::abs (loopDelayTarget - loopDelaySamples) < 1.0e-4f)
            loopDelaySamples = loopDelayTarget;
        updateDelayInterpolator();
    }

    const auto signal = readDelayLine();

    // Loop low-pass (Brightness), active in every stage so high partials always decay.
    auto smoothed = signal;
    for (int k = 0; k < loopStages; ++k)
    {
        auto& state = loopStageState[(size_t) k];
        state = (1.0f - loopStageCoeff) * smoothed + loopStageCoeff * state;
        smoothed = state;
    }
    const auto lowPassed = smoothed;

    // Loop high-pass: removes the DC mode so feedback may exceed 1.
    const auto highPassed = lowPassed - highPassIn1 + highPassCoeff * highPassOut1;
    highPassIn1  = lowPassed;
    highPassOut1 = highPassed;

    // The damper is applied to the output below, not in the loop.
    const auto filtered = highPassed;

    feedbackGain += (feedbackTarget - feedbackGain) * feedbackSmoothing;

    // Clamp to the cap: a retune can lower it while the smoothed gain lags.
    feedbackGain = juce::jmin (feedbackGain, feedbackCap);

    const auto string = excitation + feedbackGain * filtered;
    delayLine[(size_t) writeIndex] = string;

    if (++writeIndex >= (int) delayLine.size())
        writeIndex = 0;

    // Per-period energy; stage decisions are made once per period.
    periodEnergy += filtered * filtered;

    if (++periodCounter >= (int) periodSamples)
    {
        lastPeriodRms = std::sqrt (periodEnergy / (float) periodCounter);
        periodEnergy  = 0.0f;
        periodCounter = 0;
        periodCompleted();
    }

    // Output: excitation plus returning wave, before the feedback gain. Taken
    // at the write point, so the note sounds without one period of latency.
    glideDamper (false);
    auto output = applyDamper (excitation + filtered);
    if (params.subLevel > 0.0f)
    {
        // Sub level from a running mean square; the per-period RMS would modulate it.
        subMeanSquare += (filtered * filtered - subMeanSquare) * subLevelCoeff;
        output += std::sin (twoPi * subPhase) * std::sqrt (subMeanSquare) * subGain * params.subLevel;

        subPhase += subPhaseStep;
        if (subPhase >= 1.0f)
            subPhase -= 1.0f;
    }

    const auto dcFree = output - dcIn1 + dcBlockerCoeff * dcOut1;
    dcIn1  = output;
    dcOut1 = dcFree;

    // Runaway guard: silence and free the voice.
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

    // The first period after the exciter stops may be partly empty; skip it.
    ++periodsSinceExcitation;
    if (periodsSinceExcitation < 2)
        return;

    if (stage == Stage::exciting)
    {
        referenceLevel = lastPeriodRms;
        setStage (Stage::decaying);
    }
    else if (stage == Stage::decaying && params.sustainLevel > 0.0f
             && periodsSinceExcitation >= minimumPeriodsBeforeHold)
    {
        // Hold once the level drops to the sustain level, but only after
        // minimumPeriodsBeforeHold, so unsettled excitation is not frozen in.
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

    // Holding sets feedback to the cap, cancelling the loop loss at f0;
    // renderNextBlock() blends the low-pass to its hold setting.
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
