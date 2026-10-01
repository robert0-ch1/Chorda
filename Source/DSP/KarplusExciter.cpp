/*
  ==============================================================================

    KarplusExciter.cpp

    Exciter: sine/square/noise waveforms, their drive gains, the band-limiting
    filter and the pick-position comb. Audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

void KarplusVoice::prepareExcitation() noexcept
{
    // Pitch-dependent exciter setup. Redone by glideFromFrequency() for the glide start pitch.
    prepareExciterFilter();
    updateExciterMix();
    updateExcitationDrive();

    // Pick comb with delay pickPosition x period. Past the middle it notches
    // the mirrored harmonics with even ones phase-flipped, as on a real string.
    // At least one sample, so the pick is never bypassed.
    combDelaySamples = juce::jlimit (1, (int) exciterHistory.size() - 1,
                                     juce::roundToInt (params.pickPosition * periodSamples));
    combTailLeft = combDelaySamples;
}

void KarplusVoice::updateExcitationDrive() noexcept
{
    // A long excitation keeps adding to a ringing loop: tonal input sums
    // coherently (1 / periods), noise in power (1 / sqrt(periods)). The tonal
    // gain also makes up the band-limit loss at f0.
    const auto periodsInAttack = juce::jmax (1.0f, (float) attackSamplesTotal / periodSamples);

    const auto z = std::polar (1.0f, -twoPi * frequencyHz / (float) getSampleRate());
    const auto stageAtF0 = std::abs ((1.0f - exciterStageCoeff) / (1.0f - exciterStageCoeff * z));
    const auto tonalMakeup = 1.0f / juce::jmax (1.0e-3f, std::pow (stageAtF0, (float) exciterStages));

    exciterTonalDrive = tonalMakeup / periodsInAttack;
    exciterNoiseDrive = exciterNoiseMakeup / std::sqrt (periodsInAttack);
}

void KarplusVoice::prepareExciterFilter() noexcept
{
    // Band-limit the excitation with the same cascade as one loop trip, so
    // it carries no energy the loop would strip off. Otherwise noise up to
    // Nyquist circulates, which is harsh on low notes and rings while holding.
    exciterStages     = loopStages;
    exciterStageCoeff = loopStageCoeff;
    exciterStageState.fill (0.0f);

    // Make-up for equal noise power through the cascade, in closed form (O(K)).
    exciterNoiseMakeup = (float) (1.0 / std::sqrt (cascadeEnergy (exciterStages, (double) exciterStageCoeff)));
}

double KarplusVoice::cascadeEnergy (int stages, double a) noexcept
{
    // Sum of h[n]^2 for K stages of y = (1 - a) x + a y[n-1]:
    //   E = (1 - a) / (1 + a)^(2K - 1) * sum_n C(K-1, n)^2 a^(2n)
    // (Euler's transformation of 2F1(K, K; 1; a^2); the series stops at n = K - 1.)
    stages = juce::jmax (1, stages);
    a = juce::jlimit (0.0, 0.999999, a);
    double sum = 0.0, binomial = 1.0, power = 1.0;
    for (int n = 0; n < stages; ++n)
    {
        sum += binomial * binomial * power;
        binomial = binomial * (double) (stages - 1 - n) / (double) (n + 1);
        power *= a * a;
    }
    return juce::jmax (1.0e-12, (1.0 - a) / std::pow (1.0 + a, 2 * stages - 1) * sum);
}

float KarplusVoice::nextExciterSample() noexcept
{
    float raw = 0.0f;

    if (attackSamplesLeft > 0)
    {
        float tonal = 0.0f;
        if (exciterSineGain   > 0.0f) tonal += exciterSineGain   * std::sin (twoPi * exciterPhase);
        if (exciterSquareGain > 0.0f) tonal += exciterSquareGain * (exciterPhase < 0.5f ? 1.0f : -1.0f);

        raw = tonal * exciterTonalDrive;
        if (exciterNoiseGain > 0.0f)
            raw += exciterNoiseGain * (random.nextFloat() * 2.0f - 1.0f) * exciterNoiseDrive;

        // Raised-cosine window over the attack.
        const auto progress = 1.0f - (float) attackSamplesLeft / (float) attackSamplesTotal;
        const auto window   = 0.5f - 0.5f * std::cos (twoPi * progress);
        raw *= window;
        --attackSamplesLeft;

        exciterPhase += frequencyHz / (float) getSampleRate();
        if (exciterPhase >= 1.0f)
            exciterPhase -= 1.0f;
    }

    // Band-limit; the drives already compensate its loss.
    for (int k = 0; k < exciterStages; ++k)
    {
        auto& state = exciterStageState[(size_t) k];
        state = (1.0f - exciterStageCoeff) * raw + exciterStageCoeff * state;
        raw = state;
    }

    if (combDelaySamples == 0)
    {
        if (attackSamplesLeft == 0)
            exciterFinished = true;

        return raw;
    }

    // Pick comb x[n] - x[n - D]: notches the harmonics with a node at the pick.
    const auto size = (int) exciterHistory.size();
    auto readIndex = exciterWriteIndex - combDelaySamples;
    if (readIndex < 0)
        readIndex += size;

    const auto delayedRaw = exciterHistory[(size_t) readIndex];
    exciterHistory[(size_t) exciterWriteIndex] = raw;

    if (++exciterWriteIndex >= size)
        exciterWriteIndex = 0;

    // The comb outputs for combDelaySamples more after the attack ends.
    if (attackSamplesLeft == 0)
    {
        if (combTailLeft > 0)
            --combTailLeft;
        else
            exciterFinished = true;
    }

    return raw - delayedRaw;
}

void KarplusVoice::updateExciterMix() noexcept
{
    // Tone: sine -> square -> noise. Sine and square are phase-coherent, so a
    // linear crossfade; square and noise are uncorrelated, so equal power.
    const auto tone = juce::jlimit (0.0f, 1.0f, params.exciterTone);

    if (tone <= toneSquare)
    {
        const auto t = tone / toneSquare;
        exciterSineGain   = 1.0f - t;
        exciterSquareGain = t;
        exciterNoiseGain  = 0.0f;
    }
    else
    {
        const auto t = (tone - toneSquare) / (toneNoise - toneSquare);
        const auto angle = t * 0.5f * juce::MathConstants<float>::pi;
        exciterSineGain   = 0.0f;
        exciterSquareGain = t >= 1.0f ? 0.0f : std::cos (angle);   // exact at the knob end
        exciterNoiseGain  = t >= 1.0f ? 1.0f : std::sin (angle);
    }

    // Trim the tonal waveforms down to the noise loudness (see sineExcessDb).
    // Never boost: a short low sine is quiet because it is under one cycle.
    const auto note = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, frequencyHz) / 440.0f);   // pitch at pluck time
    auto trim = [note] (float excessDb, float perNote)
    {
        return juce::Decibels::decibelsToGain (-juce::jmax (0.0f, excessDb + perNote * (note - toneReferenceNote)));
    };

    exciterSineGain   *= trim (sineExcessDb,   sineExcessDbPerNote);
    exciterSquareGain *= trim (squareExcessDb, squareExcessDbPerNote);
}

} // namespace pluck
