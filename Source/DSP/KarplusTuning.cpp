/*
  ==============================================================================

    KarplusTuning.cpp

    String loop: delay length and fractional tuning, loop filters, and the
    feedback gain that sets the decay. Audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

float KarplusVoice::readDelayLine() noexcept
{
    auto index = writeIndex - integerDelay;
    if (index < 0)
        index += (int) delayLine.size();

    // Fractional delay, first-order allpass: y[n] = C x[n] + x[n-1] - C y[n-1]
    const auto x = delayLine[(size_t) index];
    const auto y = allpassCoeff * x + allpassX1 - allpassCoeff * allpassY1;
    allpassX1 = x;
    allpassY1 = y;
    return y;
}

void KarplusVoice::updateDelayInterpolator() noexcept
{
    // Fractional part kept in [0.5, 1.5), where the allpass is most accurate.
    const auto previousTap = integerDelay;
    integerDelay = juce::jmax (1, (int) std::floor (loopDelaySamples - 0.5f));
    const auto fraction = loopDelaySamples - (float) integerDelay;
    allpassCoeff = (1.0f - fraction) / (1.0f + fraction);

    // When the integer tap moves, reinitialise the allpass state for the new
    // tap (last input, and last output approximated by linear interpolation)
    // to avoid a click.
    if (integerDelay != previousTap && ! delayLine.empty())
    {
        const auto size = (int) delayLine.size();
        auto at = [&] (int index) { while (index < 0) index += size; return delayLine[(size_t) (index % size)]; };

        const auto last = writeIndex - 1;
        allpassX1 = at (last - integerDelay);

        const auto back  = (float) integerDelay + fraction;
        const auto whole = (int) std::floor (back);
        const auto part  = back - (float) whole;
        allpassY1 = at (last - whole) + part * (at (last - whole - 1) - at (last - whole));
    }
}

void KarplusVoice::updateLoopFilters() noexcept
{
    const auto sr = (float) getSampleRate();

    // One-pole low-pass, cutoff modulated by velocity and mod wheel, at least
    // minimumBrightnessRatio x f0.
    const auto modulated = params.brightnessHz * velocityBrightness
                         * std::pow (2.0f, modWheelBrightnessOctaves * params.modWheel);
    const auto cutoff = juce::jlimit (20.0f, 0.49f * sr, juce::jmax (modulated, minimumBrightnessRatio * frequencyHz));
    lowPassCoeff = std::exp (-twoPi * cutoff / sr);

    // Below brightnessReferenceHz, use N stages with a total loss at the cutoff
    // of (reference / f0) times one stage, so loss per second matches the
    // reference. One stage at and above it.
    const auto trips = brightnessReferenceHz / frequencyHz;
    const auto previousStages = loopStages;
    if (trips <= 1.0f)
    {
        loopStages = 1;
        loopStageCoeff = lowPassCoeff;
    }
    else
    {
        loopStages = juce::jlimit (1, maximumLoopStages, (int) std::ceil (trips));
        const auto singleLossDb = -10.0f * std::log10 (std::norm (lowPassResponse (cutoff)));   // one stage, at the cutoff
        loopStageCoeff = onePoleCoeffForLoss (twoPi * cutoff / sr,
                                              singleLossDb * juce::jmin (trips, (float) maximumLoopStages) / (float) loopStages);
    }

    // While holding, blend towards the light low-pass (see holdLossDbPerSecond).
    if (holdBlend > 0.0f)
    {
        const auto holdCoeff = onePoleCoeffForLoss (twoPi * cutoff / sr,
                                                    holdLossDbPerSecond / juce::jmax (1.0f, frequencyHz) / (float) loopStages);
        loopStageCoeff += (holdCoeff - loopStageCoeff) * holdBlend;
    }

    // New stages start from the last stage's state, not from zero.
    for (int k = previousStages; k < loopStages; ++k)
        loopStageState[(size_t) k] = loopStageState[(size_t) juce::jmax (0, previousStages - 1)];

    highPassCutoffHz = frequencyHz * loopHighPassRatio;
    highPassCoeff    = 1.0f - twoPi * highPassCutoffHz / sr;

    // Damper delay is a fraction of the period, so it tracks pitch.
    updateDamperFilter();
}

std::complex<float> KarplusVoice::smoothLoopResponse (float hz) const noexcept
{
    // Loop response excluding the pure delay and the unity-gain allpass.
    const auto w = twoPi * hz / (float) getSampleRate();
    const auto z = std::polar (1.0f, -w);                                  // e^(-jw)

    auto response = (1.0f - z) / (1.0f - highPassCoeff * z);               // high-pass

    response *= loopLowPassResponse (z);                                   // low-pass, all its stages

    return response;
}

float KarplusVoice::highestResonanceGain() const noexcept
{
    // Peak loop filter gain over the possible resonances: f0, the low
    // resonance from the high-pass phase lead, and 1.85 f0 to Nyquist
    // (margin below 2 f0). Used for the stability cap.
    const auto sr = (float) getSampleRate();
    const auto nyquist = 0.5f * sr;

    auto smoothGain = [&] (float hz)
    {
        const auto zz = std::polar (1.0f, -twoPi * hz / sr);
        auto response = (1.0f - zz) / (1.0f - highPassCoeff * zz);
        response *= loopLowPassResponse (zz);
        return std::abs (response);
    };

    auto peak = juce::jmax (smoothGain (frequencyHz),
                            smoothGain (std::sqrt (highPassCutoffHz * frequencyHz / twoPi)));

    // Log grid; the HP x LP product is smooth, so 24 points suffice.
    constexpr int points = 24;
    const auto lowest = juce::jmin (1.85f * frequencyHz, nyquist * 0.999f);
    for (int i = 0; i <= points; ++i)
    {
        const auto hz = lowest * std::pow (nyquist / lowest, (float) i / (float) points);
        peak = juce::jmax (peak, smoothGain (hz));
    }

    return juce::jmax (peak, 1.0e-3f);
}

void KarplusVoice::updateTuning() noexcept
{
    const auto sr = (float) getSampleRate();
    if (sr <= 0.0f)
        return;

    const auto note = (float) soundingNote + (float) params.transposeSemitones + pitchBend * pitchBendRangeSemitones;
    targetFrequencyHz = 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);

    // While gliding, the glide target follows bend and transpose.
    if (gliding)
    {
        glideTargetLog = std::log2 (targetFrequencyHz);
        frequencyHz    = glideFrequencyHz;
    }
    else
    {
        frequencyHz = targetFrequencyHz;
    }

    // Clamp to what the delay line can hold, so filter and cap maths match the played pitch.
    frequencyHz = juce::jlimit (lowestFrequencyHz * 1.02f, 0.45f * sr, frequencyHz);

    periodSamples = sr / frequencyHz;                     // nominal loop length, in samples

    updateLoopFilters();

    // Subtract the loop filters' phase delay at f0 from the delay length.
    const auto w = twoPi * frequencyHz / sr;
    const auto response = smoothLoopResponse (frequencyHz);
    const auto phaseDelay = -std::arg (response) / w;                     // samples; negative = advance

    // renderSample() glides the delay towards this target; stepping it on
    // block-rate retunes would cause zipper noise.
    loopDelayTarget = juce::jlimit (2.0f, (float) delayLine.size() - 3.0f, periodSamples - phaseDelay);
    if (snapLoopDelay)
    {
        loopDelaySamples = loopDelayTarget;
        updateDelayInterpolator();
    }

    loopFilterGainAtF0 = juce::jmax (std::abs (response), 1.0e-3f);
    feedbackCap        = maximumLoopGain / highestResonanceGain();

    // Sub follows the actual loop period, not the nominal one.
    subPhaseStep = 0.5f / juce::jmax (2.0f, actualPeriodSamples());

    // Exciter drive is per period, so rescale it if the pitch moves during the attack.
    if (! exciterFinished && attackSamplesLeft > 0)
        updateExcitationDrive();
}

std::complex<float> KarplusVoice::loopLowPassResponse (std::complex<float> z) const noexcept
{
    const auto one = (1.0f - loopStageCoeff) / (1.0f - loopStageCoeff * z);
    auto response = one;
    for (int k = 1; k < loopStages; ++k)
        response *= one;
    return response;
}

std::complex<float> KarplusVoice::lowPassResponse (float hz) const noexcept
{
    const auto z = std::polar (1.0f, -twoPi * hz / (float) getSampleRate());
    return (1.0f - lowPassCoeff) / (1.0f - lowPassCoeff * z);
}

float KarplusVoice::actualPeriodSamples() const noexcept
{
    // Integer tap + allpass phase delay at f0 + loop filter phase delay.
    // Each phase term is small enough not to wrap.
    const auto w = twoPi * frequencyHz / (float) getSampleRate();
    if (w <= 0.0f)
        return periodSamples;

    const auto z = std::polar (1.0f, -w);
    const auto allpass = (allpassCoeff + z) / (1.0f + allpassCoeff * z);

    return (float) integerDelay - std::arg (allpass) / w - std::arg (smoothLoopResponse (frequencyHz)) / w;
}

float KarplusVoice::feedbackForT60 (float seconds) const noexcept
{
    // -60 dB over T60 * f0 round trips: g = 0.001^(1 / (T60 f0)) / |H(f0)|,
    // limited by the stability cap.
    const auto periodsInT60 = juce::jmax (1.0f, seconds * frequencyHz);
    const auto gain = std::pow (0.001f, 1.0f / periodsInT60) / loopFilterGainAtF0;
    return juce::jlimit (0.0f, feedbackCap, gain);
}

void KarplusVoice::updateFeedbackTarget() noexcept
{
    switch (stage)
    {
        case Stage::exciting:
        case Stage::decaying:  feedbackTarget = feedbackForT60 (params.decaySeconds);   break;
        case Stage::holding:   feedbackTarget = feedbackCap;                            break;
        case Stage::releasing: feedbackTarget = feedbackForT60 (params.releaseSeconds); break;
        case Stage::idle:      break;
    }
}

} // namespace pluck
