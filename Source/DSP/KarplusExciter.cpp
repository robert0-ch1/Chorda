/*
  ==============================================================================

    KarplusExciter.cpp

    Exciter: sine/square/noise waveforms, their drive gains, the band-limiting
    filter and the pick-position comb. Audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace
{
    /** Power weight of the ear at f: the K-weighting of ITU-R BS.1770,
        a 38 Hz high-pass and a +4 dB shelf above about 1.5 kHz. */
    float hearingWeight (float f) noexcept
    {
        constexpr float highPassHz = 38.0f, shelfHz = 1500.0f, shelfGain = 2.5f;
        const auto f2 = f * f, c2 = highPassHz * highPassHz, r = f2 / (shelfHz * shelfHz);
        const auto highPass = f2 * f2 / ((f2 - c2) * (f2 - c2) + 4.0f * f2 * c2);
        return highPass * (1.0f + shelfGain * r) / (1.0f + r);
    }
}

namespace pluck
{

using namespace voice;

void KarplusVoice::prepareExcitation() noexcept
{
    // Pitch-dependent exciter setup. Redone by glideFromFrequency() for the glide start pitch.
    prepareExciterFilter();
    updateExciterMix();
    preparePluckShape();
    updateExcitationDrive();

    // Pick comb with delay pickPosition x period. Past the middle it notches
    // the mirrored harmonics with even ones phase-flipped, as on a real string.
    // At least one sample, so the pick is never bypassed.
    combDelaySamples = juce::jlimit (1, (int) exciterHistory.size() - 1,
                                     juce::roundToInt (params.pickPosition * periodSamples));
    // Let the pluck shaper ring down to -120 dB before the exciter is finished:
    // two one-poles decay as (1 + an) e^-an, below 1e-6 by about an = 17.
    auto shaperTail = 0;
    if (pluckShapeCoeff > 0.0f)
        shaperTail = juce::roundToInt (17.0f / juce::jmax (1.0e-4f, -std::log (pluckShapeCoeff)));
    combTailLeft = combDelaySamples + shaperTail;
}

void KarplusVoice::preparePluckShape() noexcept
{
    // Pluck shape: two one-poles at a key-tracked cutoff give the roll-off of a
    // real pluck, so low notes fill the period instead of carrying sharp blips.
    // Full up to C2, fading out by C4, at any attack length.
    {
        const auto periods = (float) attackSamplesTotal / periodSamples;
        const auto octavesAboveFull = std::log2 (juce::jmax (1.0f, frequencyHz) / pluckShapeFullBelowHz);
        const auto amount  = juce::jlimit (0.0f, 1.0f, 1.0f - octavesAboveFull / pluckShapeFadeOctaves);
        if (amount <= 0.0f)
        {
            pluckShapeCoeff     = 0.0f;
            pluckShapeGain       = 1.0f;
            pluckShapeTonalGain  = 1.0f;
            pluckShapeSquareGain = 1.0f;
        }
        else
        {
            const auto sr = (float) getSampleRate();
            const auto cutoff = juce::jmin (0.45f * sr, juce::jmax (pluckShapeMinimumHz, frequencyHz) * std::pow (40.0f, 1.0f - amount));
            pluckShapeCoeff = std::exp (-twoPi * cutoff / sr);

            // Keep the pluck's loudness: compare its harmonics with and without
            // the shaper, each weighted by the pick comb, the pluck's band-limit,
            // how long it rings in the loop and how loud the ear hears it.
            const auto feedback = feedbackForT60 (params.decaySeconds);
            const auto harmonics = juce::jlimit (1, 64, (int) (0.45f * sr / juce::jmax (1.0f, frequencyHz)));
            float before = 0.0f, after = 0.0f;
            for (int n = 1; n <= harmonics; ++n)
            {
                const auto comb = std::sin ((float) n * juce::MathConstants<float>::pi * params.pickPosition);
                const auto zn = std::polar (1.0f, -twoPi * (float) n * frequencyHz / sr);
                const auto onePole = std::norm ((1.0f - pluckShapeCoeff) / (1.0f - pluckShapeCoeff * zn));
                const auto hz = (float) n * frequencyHz;
                const auto bandLimit = std::pow (std::norm ((1.0f - exciterStageCoeff) / (1.0f - exciterStageCoeff * zn)), (float) exciterStages);
                const auto roundTrip = juce::jmin (0.999f, feedback * feedback * std::norm (smoothLoopResponse (hz)));
                const auto weight = comb * comb * bandLimit / (1.0f - roundTrip) * hearingWeight (hz);
                before += weight;
                after  += weight * onePole * onePole;
            }
            // Capped: with the pick near an end almost nothing reaches the low
            // harmonics, and a full make-up would overshoot once the pick moves.
            pluckShapeGain = juce::jmin (pluckShapeMaxGain, std::sqrt (before / juce::jmax (1.0e-9f, after)));

            // A sine or square burst of a period or more carries its energy at
            // f0, so keep that level; a shorter one is a click, spread more like
            // noise. Halfway between the two fits the measured levels.
            const auto zf = std::polar (1.0f, -twoPi * frequencyHz / sr);
            const auto atF0 = 1.0f / juce::jmax (1.0e-4f, std::norm ((1.0f - pluckShapeCoeff) / (1.0f - pluckShapeCoeff * zf)));
            const auto tonal = juce::jlimit (0.0f, 1.0f, periods);
            pluckShapeTonalGain = std::pow (pluckShapeGain, 0.5f * (1.0f - tonal)) * std::pow (atF0, 0.5f + 0.5f * tonal);

            // A square keeps strong harmonics at any length, so it stays nearer
            // halfway (fitted to measured levels).
            const auto squareSpread = 0.5f - 0.25f * tonal;
            pluckShapeSquareGain = std::pow (pluckShapeGain, squareSpread) * std::pow (atF0, 1.0f - squareSpread);
        }
    }

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

    // A burst shorter than one period is much quieter than a one-period
    // burst: up to 24 dB for a sine at 0.5 ms on A2. The loss depends only on
    // the fraction x of a period the burst covers, so each waveform gets a
    // make-up fitted to measured levels (sine ~26 dB per decade below
    // x = 0.4, square ~7.5, noise ~11). The cap keeps the burst peak bounded.
    exciterSineBoost = exciterSquareBoost = 1.0f;
    const auto x = (float) attackSamplesTotal / periodSamples;
    if (x > 0.0f && x < 1.0f)
    {
        const auto peakLimit = maxShortBurstPeak / juce::jmin (1.0f, juce::MathConstants<float>::pi * x);
        exciterSineBoost   = juce::jmin (peakLimit, x < 0.4f ? std::pow (0.4f / x, 1.3f) : 1.0f);
        exciterSquareBoost = juce::jmin (maxShortBurstPeak, std::pow (x, -0.375f));
        exciterNoiseDrive  = exciterNoiseMakeup * juce::jmin (maxShortBurstPeak, std::pow (x, -0.55f));
    }
}

void KarplusVoice::prepareExciterFilter() noexcept
{
    // Band-limit the excitation with the same cascade as one loop trip, so
    // it carries no energy the loop would strip off. Otherwise noise up to
    // Nyquist circulates, which is harsh on low notes and rings while holding.
    exciterStages     = loopStages;
    exciterStageCoeff = loopStageCoeff;
    exciterStageState.fill (0.0f);
    pluckShapeState.fill (0.0f);

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
        if (exciterSineGain   > 0.0f) tonal += exciterSineGain   * exciterSineBoost   * pluckShapeTonalGain  * std::sin (twoPi * exciterPhase);
        if (exciterSquareGain > 0.0f) tonal += exciterSquareGain * exciterSquareBoost * pluckShapeSquareGain * (exciterPhase < 0.5f ? 1.0f : -1.0f);

        raw = tonal * exciterTonalDrive;
        if (exciterNoiseGain > 0.0f)
            raw += exciterNoiseGain * (random.nextFloat() * 2.0f - 1.0f) * exciterNoiseDrive * pluckShapeGain;

        // Raised-cosine window over the attack.
        const auto progress = 1.0f - (float) attackSamplesLeft / (float) attackSamplesTotal;
        const auto window   = 0.5f - 0.5f * std::cos (twoPi * progress);
        raw *= window;
        --attackSamplesLeft;

        exciterPhase += frequencyHz / (float) getSampleRate();
        if (exciterPhase >= 1.0f)
            exciterPhase -= 1.0f;
    }

    if (pluckShapeCoeff > 0.0f)
        for (auto& state : pluckShapeState)
        {
            state = (1.0f - pluckShapeCoeff) * raw + pluckShapeCoeff * state;
            raw = state;
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
