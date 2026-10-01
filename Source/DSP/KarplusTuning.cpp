/*
  ==============================================================================

    KarplusTuning.cpp

    The string loop: delay length and fractional tuning, the loop filters,
    and the feedback that sets how fast the string decays.
    Part of KarplusVoice; everything here runs on the audio thread.

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

    // First-order allpass supplies the fractional part of the delay:
    // y[n] = C x[n] + x[n-1] - C y[n-1]
    const auto x = delayLine[(size_t) index];
    const auto y = allpassCoeff * x + allpassX1 - allpassCoeff * allpassY1;
    allpassX1 = x;
    allpassY1 = y;
    return y;
}

void KarplusVoice::updateDelayInterpolator() noexcept
{
    // Split the delay into an integer tap and a fractional part in [0.5, 1.5),
    // the range where the allpass approximation is most accurate.
    const auto previousTap = integerDelay;
    integerDelay = juce::jmax (1, (int) std::floor (loopDelaySamples - 0.5f));
    const auto fraction = loopDelaySamples - (float) integerDelay;
    allpassCoeff = (1.0f - fraction) / (1.0f + fraction);

    // When the tap moves by a sample (a retune: glide, the hold easing in),
    // the allpass's memory belongs to the old tap, and carrying it over is a
    // click. Give it what it would have held at the new one: the last input
    // it would have read, and the last output it would have made, which is
    // the string read at the full fractional delay a sample ago.
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

    // One-pole low-pass, never closer than minimumBrightnessRatio to f0. Velocity
    // and the mod wheel both push the cutoff around the knob's value.
    const auto modulated = params.brightnessHz * velocityBrightness
                         * std::pow (2.0f, modWheelBrightnessOctaves * params.modWheel);
    const auto cutoff = juce::jlimit (20.0f, 0.49f * sr, juce::jmax (modulated, minimumBrightnessRatio * frequencyHz));
    lowPassCoeff = std::exp (-twoPi * cutoff / sr);

    // Below the reference note, run the loop low-pass as N stages whose
    // loss at the cutoff adds up to (reference / f0) times what one stage
    // loses there: per second, the same as at the reference. At and above it,
    // one stage, exactly as before. The exciter keeps the single stage, so
    // the pluck's level is unchanged.
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
        const auto singleLossDb = -10.0f * std::log10 (std::norm (lowPassResponse (cutoff)));   // one stage at the cutoff
        loopStageCoeff = onePoleCoeffForLoss (twoPi * cutoff / sr,
                                              singleLossDb * juce::jmin (trips, (float) maximumLoopStages) / (float) loopStages);
    }

    // Holding: glide towards the light low-pass (see holdLossDbPerSecond).
    if (holdBlend > 0.0f)
    {
        const auto holdCoeff = onePoleCoeffForLoss (twoPi * cutoff / sr,
                                                    holdLossDbPerSecond / juce::jmax (1.0f, frequencyHz) / (float) loopStages);
        loopStageCoeff += (holdCoeff - loopStageCoeff) * holdBlend;
    }

    // Stages brought in by a glide start from where the last one is, not from silence.
    for (int k = previousStages; k < loopStages; ++k)
        loopStageState[(size_t) k] = loopStageState[(size_t) juce::jmax (0, previousStages - 1)];

    // One-pole high-pass a long way below f0.
    highPassCutoffHz = frequencyHz * loopHighPassRatio;
    highPassCoeff    = 1.0f - twoPi * highPassCutoffHz / sr;

    // Damper filter: the delay is a fraction of the period, so it follows
    // the note (and a glide).
    updateDamperFilter();
}

std::complex<float> KarplusVoice::smoothLoopResponse (float hz) const noexcept
{
    // The smooth part of one trip round the loop, apart from the pure delay:
    // the low-pass and the high-pass. (The fractional allpass is unity.)
    const auto w = twoPi * hz / (float) getSampleRate();
    const auto z = std::polar (1.0f, -w);                                  // e^(-jw)

    auto response = (1.0f - z) / (1.0f - highPassCoeff * z);               // high-pass

    response *= loopLowPassResponse (z);                                   // low-pass, all its stages

    return response;
}

float KarplusVoice::highestResonanceGain() const noexcept
{
    // Stability needs the loop gain below 1 at every resonance. The tuning
    // pins the first resonance to f0 exactly, and the others sit near its
    // multiples, never below 1.85 f0 (a margin kept from when the damper
    // was in the loop and could move them). There is also the spurious low
    // resonance where the high-pass's phase lead lines up with the delay.
    // Bound the loop response (low-pass and high-pass) at those places.
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

    // Everything from 1.85 f0 up to Nyquist, on a log grid. The product of a
    // rising high-pass and a falling low-pass is smooth, so 24 points suffice.
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

    // While gliding the string sits where the glide has reached, not where the
    // key says; the glide's own target is kept in step with the key, so bend
    // and the transposer still work in the middle of one.
    if (gliding)
    {
        glideTargetLog = std::log2 (targetFrequencyHz);
        frequencyHz    = glideFrequencyHz;
    }
    else
    {
        frequencyHz = targetFrequencyHz;
    }

    // The delay line holds a period down to lowestFrequencyHz. Below that (an
    // octave or two down from the bottom of the keyboard) the loop could not
    // be as long as asked, every filter sum would be done for a pitch it is
    // not playing, and the stability cap with them. Hold the pitch there.
    frequencyHz = juce::jlimit (lowestFrequencyHz * 1.02f, 0.45f * sr, frequencyHz);

    periodSamples = sr / frequencyHz;                     // total loop length we want, in samples

    updateLoopFilters();

    // The loop filters delay the fundamental; subtract that from the delay
    // line length or notes play out of tune.
    const auto w = twoPi * frequencyHz / sr;
    const auto response = smoothLoopResponse (frequencyHz);
    const auto phaseDelay = -std::arg (response) / w;                     // samples; negative = advance

    // The loop length glides to its new value sample by sample (see
    // renderSample) rather than jumping there. A retune arrives once a block
    // at best (a pitch-wheel message, a glide step), and reading a delay line
    // whose length steps is a zipper: under a smooth pitch bend that put the
    // energy above 4 kHz at -45 dB where the held note has -110.
    loopDelayTarget = juce::jlimit (2.0f, (float) delayLine.size() - 3.0f, periodSamples - phaseDelay);
    if (snapLoopDelay)
    {
        loopDelaySamples = loopDelayTarget;
        updateDelayInterpolator();
    }

    // What the loop does to the fundamental, and how far the feedback may go.
    loopFilterGainAtF0 = juce::jmax (std::abs (response), 1.0e-3f);
    feedbackCap        = maximumLoopGain / highestResonanceGain();

    // The sub must follow the string's real period, not the nominal one.
    subPhaseStep = 0.5f / juce::jmax (2.0f, actualPeriodSamples());

    // A long pluck still feeding the string when the pitch moves (a glide, a
    // bend, an octave change) has to follow it: the drive is per period, and
    // the same drive at a pitch six octaves up lands on 64 times as many
    // periods and piles up (measured: 30x full scale on a glide up during a
    // long attack).
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
    // The delay the signal really sees on one trip: the integer tap, plus the
    // fraction the allpass supplies at f0 (which is not quite the fraction it
    // was designed for), plus the phase delay of the loop filters. Each term
    // is small enough on its own that the phase never wraps.
    const auto w = twoPi * frequencyHz / (float) getSampleRate();
    if (w <= 0.0f)
        return periodSamples;

    const auto z = std::polar (1.0f, -w);
    const auto allpass = (allpassCoeff + z) / (1.0f + allpassCoeff * z);

    return (float) integerDelay - std::arg (allpass) / w - std::arg (smoothLoopResponse (frequencyHz)) / w;
}

float KarplusVoice::feedbackForT60 (float seconds) const noexcept
{
    // After `seconds` the string should have fallen by 60 dB (a factor of
    // 0.001). There are seconds * f0 round trips in that time, so each one may
    // keep 0.001^(1 / (T60 * f0)). The loop filters take their own share, so
    // divide that out; the cap keeps every resonance below unity gain.
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
