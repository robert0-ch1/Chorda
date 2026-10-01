/*
  ==============================================================================

    KarplusExciter.cpp

    The pluck: the three waveforms, how hard each drives the string, and the
    filter that band-limits it to what the string can carry.
    Part of KarplusVoice; everything here runs on the audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

void KarplusVoice::prepareExcitation() noexcept
{
    // Everything about the pluck that depends on the pitch the string is at
    // when it is plucked. A glide starts the string at the last note, not at
    // this one, so it is worked out again then (see glideFromFrequency).
    //
    prepareExciterFilter();
    //
    // The loop accumulates whatever is fed in, so a long excitation would get
    // louder and louder. Scale it so the resulting level stays roughly constant:
    // noise adds up in power (sqrt), a waveform at f0 adds up coherently.
    updateExciterMix();

    // A long excitation keeps adding to a loop that is already ringing, so the
    // longer it is the less each sample may contribute. A waveform at f0 adds
    // up in step, noise adds up in power.
    //
    // The excitation is also band-limited by the same low-pass as the loop,
    // because a real pluck cannot put energy into partials the string will not
    // carry. (Without this the noise exciter fills the loop up to Nyquist, and
    // a held note, which stops damping the string, would ring on that for
    // ever.) That costs a tone and a noise burst different amounts, so each is
    // given back its own. The low-pass is linear, so handing each waveform its
    // own drive before the filter comes to exactly the same thing as the single
    // gain after it that this used to be.
    updateExcitationDrive();

    // Pick position: a comb filter whose notch spacing matches the distance
    // between the pluck point and the bridge, over the whole string. Past the
    // middle it notches the same harmonics as the mirrored point and flips
    // the phases of the even ones, as a real string does: the two waves the
    // pluck sends out reach the bridge in the other order. The pick is never
    // off, so the delay is at least one sample even for the highest notes.
    combDelaySamples = juce::jlimit (1, (int) exciterHistory.size() - 1,
                                     juce::roundToInt (params.pickPosition * periodSamples));
    combTailLeft = combDelaySamples;
}

void KarplusVoice::updateExcitationDrive() noexcept
{
    // How hard each waveform drives the string, for the pitch it is at now.
    const auto periodsInAttack = juce::jmax (1.0f, (float) attackSamplesTotal / periodSamples);

    const auto z = std::polar (1.0f, -twoPi * frequencyHz / (float) getSampleRate());
    const auto stageAtF0 = std::abs ((1.0f - exciterStageCoeff) / (1.0f - exciterStageCoeff * z));
    const auto tonalMakeup = 1.0f / juce::jmax (1.0e-3f, std::pow (stageAtF0, (float) exciterStages));

    exciterTonalDrive = tonalMakeup / periodsInAttack;
    exciterNoiseDrive = exciterNoiseMakeup / std::sqrt (periodsInAttack);
}

void KarplusVoice::prepareExciterFilter() noexcept
{
    // The pluck is band-limited by as much as one trip round the loop takes
    // away: the same stages, the same coefficient. A string cannot carry what
    // its loop strips off, and on a low note, where the loop is a cascade,
    // feeding it in anyway was a hard click once a period until the slow
    // trips wore it down: the harshness of low noise plucks. At and above
    // the reference note this is the one stage it always was.
    exciterStages     = loopStages;
    exciterStageCoeff = loopStageCoeff;
    exciterStageState.fill (0.0f);

    // Make-up for equal noise power through the cascade. The energy of K
    // identical one-poles has a closed form, so this costs K steps, not a
    // summed impulse response (that was up to 640k operations per low note).
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

        // Raised-cosine window over the attack: starts and ends without a step.
        const auto progress = 1.0f - (float) attackSamplesLeft / (float) attackSamplesTotal;
        const auto window   = 0.5f - 0.5f * std::cos (twoPi * progress);
        raw *= window;
        --attackSamplesLeft;

        exciterPhase += frequencyHz / (float) getSampleRate();
        if (exciterPhase >= 1.0f)
            exciterPhase -= 1.0f;
    }

    // Band-limit: one pole, the same cutoff the loop uses. Each waveform has
    // already been given the gain that makes up for what this takes.
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

    // Pick-position comb: subtract the excitation delayed by the round trip
    // from the pluck point to the bridge. Notches fall on the harmonics that
    // have a node at the pick position, just like on a real guitar.
    const auto size = (int) exciterHistory.size();
    auto readIndex = exciterWriteIndex - combDelaySamples;
    if (readIndex < 0)
        readIndex += size;

    const auto delayedRaw = exciterHistory[(size_t) readIndex];
    exciterHistory[(size_t) exciterWriteIndex] = raw;

    if (++exciterWriteIndex >= size)
        exciterWriteIndex = 0;

    // The delayed copy keeps arriving for combDelaySamples after the excitation ends.
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
    // Tone runs sine -> square -> noise, crossfading each neighbouring pair.
    // The two halves fade differently because the pairs are differently
    // related: a sine and a square share a phase and add up, so a straight
    // crossfade keeps the level even, while noise is unrelated to the square
    // and the two add up in power, which wants an equal-power fade. Either way
    // the ends of the knob are exactly the waveform they name.
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
        exciterSquareGain = t >= 1.0f ? 0.0f : std::cos (angle);   // exact at the end of the knob
        exciterNoiseGain  = t >= 1.0f ? 1.0f : std::sin (angle);
    }

    // Bring the two tones down to the noise burst's loudness (see
    // sineExcessDb). Never up: a low sine on a short attack is quieter than
    // the noise because it is shorter than one cycle, and boosting that would
    // only hand the headroom back.
    const auto note = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, frequencyHz) / 440.0f);   // the pitch it is plucked at
    auto trim = [note] (float excessDb, float perNote)
    {
        return juce::Decibels::decibelsToGain (-juce::jmax (0.0f, excessDb + perNote * (note - toneReferenceNote)));
    };

    exciterSineGain   *= trim (sineExcessDb,   sineExcessDbPerNote);
    exciterSquareGain *= trim (squareExcessDb, squareExcessDbPerNote);
}

} // namespace pluck
