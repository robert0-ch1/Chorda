/*
  ==============================================================================

    VoiceConstants.h

    Tuning constants of the string model, shared by the KarplusVoice sources.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck::voice
{

    /** Sizes the delay line. MIDI note 0 at full downward bend is about 7.3 Hz. */
    constexpr float lowestFrequencyHz = 7.0f;

    /** Upper bound on the loop gain at any resonance. */
    constexpr float maximumLoopGain = 0.99995f;

    /** Minimum loop low-pass cutoff as a multiple of f0, so the loss at f0 stays
        small enough to compensate with feedback. */
    constexpr float minimumBrightnessRatio = 5.0f;

    /** Loop high-pass corner as a fraction of f0: leaves f0 alone, removes the
        DC mode that a feedback gain above 1 would excite. */
    constexpr float loopHighPassRatio = 1.0f / 100.0f;

    /** Sub sine peak relative to the string RMS at full level. */
    constexpr float subGain = 1.6f;

    /** Time constant of the sub level follower, in seconds. */
    constexpr float subLevelSeconds = 0.02f;

    /** Number of feed-forward combs in the damper, ((1 - m) + m z^-D)^N. Partials
        with a node at the finger pass, others are cut by |cos| of half their
        phase. FIR, so it rings for at most N x D samples. */
    constexpr int damperStages = 6;

    /** Comb depth at full pressure: 0.5 is a complete notch. */
    constexpr float maximumDamperMix = 0.5f;

    /** Damper level make-up: follower time constant (s), max gain, update interval (samples). */
    constexpr float damperLevelSeconds = 0.03f;
    constexpr float damperMakeupLimit  = 16.0f;   // +24 dB
    constexpr int   damperMakeupInterval = 32;

    /** Glide time for damper position and depth. Position updates arrive per
        block, so stepping them would crackle. */
    constexpr float damperGlideSeconds = 0.015f;

    /** Glide time for the loop length on retune. */
    constexpr float loopDelayGlideSeconds = 0.002f;

    /** Fade-out of a voice cut off by stealing or all-notes-off. */
    constexpr float cutFadeSeconds = 0.008f;

    /** Minimum legato slide time. Changing the loop length within one period
        leaves an audible kink in the waveform. */
    constexpr float legatoMinimumSlideSeconds = 0.02f;

    /** Cutoff of the output DC blocker. */
    constexpr float dcBlockerHz = 10.0f;

    /** Round trips after the excitation before the string may hold. The loop
        low-pass removes the pluck's top end per period, so holding earlier
        would freeze undamped excitation in the loop. */
    constexpr int minimumPeriodsBeforeHold = 24;

    /** String RMS (-120 dBFS) below which a voice is freed. */
    constexpr float silenceThreshold = 1.0e-6f;

    /** In-loop level treated as a runaway. A working string stays below about 2. */
    constexpr float runawayLevel = 16.0f;

    /** Headroom so full-velocity chords do not clip before the master gain. */
    constexpr float voiceGainTrim = 0.5f;

    /** Below this f0 the loop low-pass becomes a cascade of up to
        maximumLoopStages, so high partials decay at the same rate in seconds
        as at C3. The filter acts once per period, and lower notes make fewer
        trips per second. */
    constexpr float brightnessReferenceHz = 130.81f;   // C3
    constexpr int   maximumLoopStages     = 16;

    /** While holding, the loop low-pass blends to a light filter losing this
        much per second at the Brightness cutoff, so the level holds but no
        excitation is frozen in. The blend is gradual because it moves the loop
        delay. */
    constexpr float holdLossDbPerSecond = 2.0f;
    constexpr float holdBlendSeconds    = 0.15f;
    constexpr int   holdUpdateInterval  = 32;

    /** Coefficient of a one-pole low-pass that loses lossDb at w. */
    inline float onePoleCoeffForLoss (float w, float lossDb) noexcept
    {
        const auto g2 = std::pow (10.0f, -lossDb / 10.0f);
        const auto p = 1.0f - g2 * std::cos (w), q = 1.0f - g2;
        if (q <= 1.0e-7f)
            return 0.0f;   // no loss
        // Root below 1 of (1 - a)^2 = g2 (1 - 2 a cos w + a^2).
        return juce::jlimit (0.0f, 0.9999f, (p - std::sqrt (juce::jmax (0.0f, p * p - q * q))) / q);
    }

    /** Retune interval while gliding, in samples. */
    constexpr int glideUpdateInterval = 32;

    /** Loudness excess of the sine and square exciters over the noise burst,
        at toneReferenceNote (A#3) plus a slope per semitone. Tonal exciters put
        all their energy on the string's partials. Used to trim them to the
        noise level. */
    constexpr float sineExcessDb           = 5.0f;
    constexpr float sineExcessDbPerNote    = 0.144f;
    constexpr float squareExcessDb         = 8.0f;
    constexpr float squareExcessDbPerNote  = 0.095f;
    constexpr float toneReferenceNote      = 58.0f;

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

} // namespace pluck::voice
