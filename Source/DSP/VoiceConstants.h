/*
  ==============================================================================

    VoiceConstants.h

    The tuning constants of the string model, shared by the KarplusVoice
    source files.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck::voice
{

    /** MIDI note 0 bent down by the full pitch-bend range is about 7.3 Hz; the
        delay line is sized so even that period fits. */
    constexpr float lowestFrequencyHz = 7.0f;

    /** The loop gain at any resonance must stay below this, or the string
        would ring for ever (or grow). */
    constexpr float maximumLoopGain = 0.99995f;

    /** The loop low-pass never sits closer than this to the fundamental, so
        its loss at f0 stays small enough to compensate. High notes therefore
        cannot be made darker than a couple of partials, which matches how
        real high strings behave. */
    constexpr float minimumBrightnessRatio = 5.0f;

    /** The loop high-pass corner as a fraction of f0. Low enough to leave the
        fundamental alone (so "hold" really holds), high enough to kill the DC
        mode that a feedback gain above 1 would otherwise excite. */
    constexpr float loopHighPassRatio = 1.0f / 100.0f;

    /** Sub sine peak relative to the string RMS at full level. */
    constexpr float subGain = 1.6f;

    /** Time constant of the sub's level follower, in seconds. Long enough to
        ignore the waveform, short enough to follow a pluck. */
    constexpr float subLevelSeconds = 0.02f;

    /** The damper is a cascade of this many feed-forward combs,
        ((1 - m) + m z^-D)^N, after the string. Each passes the partials with
        a node at the finger untouched and cuts the others by |cos| of half
        their phase, so the cascade picks the node partials out sharply. It is
        an FIR, so it rings for N x D samples at most (three periods at the
        middle of the string): the envelope is not smeared. */
    constexpr int damperStages = 6;

    /** Depth of each comb at full pressure: 0.5 is a complete notch. */
    constexpr float maximumDamperMix = 0.5f;

    /** The damper takes energy out, and a make-up gain puts the level back,
        measured on the string going in against the sound coming out. These
        set how fast it follows and how far it may go. */
    constexpr float damperLevelSeconds = 0.03f;
    constexpr float damperMakeupLimit  = 16.0f;   // +24 dB
    constexpr int   damperMakeupInterval = 32;

    /** The damper glides to where it is set rather than jumping there. The
        position arrives once per block from a drag, and stepping a tap that
        far apart crackled; switching the filter on straight onto a stale
        history clicked. */
    constexpr float damperGlideSeconds = 0.015f;

    /** The loop length glides to a new tuning over about this long. */
    constexpr float loopDelayGlideSeconds = 0.002f;

    /** A string that is cut off (a voice stolen for a new note, as every
        note in Mono is, or an "all notes off") fades out over this long
        instead of stopping dead. Stopping dead was a step in the output as
        big as the string itself: a loud click between every two notes. */
    constexpr float cutFadeSeconds = 0.008f;

    /** A legato slide takes at least this long, whatever Glide says. The
        loop cannot change length at once: a jump in less than a period
        leaves a kink in the wave where the old length was cut short, and it
        clicks. Spread over a few round trips the kink is too small to hear. */
    constexpr float legatoMinimumSlideSeconds = 0.02f;

    /** Cutoff of the output DC blocker. */
    constexpr float dcBlockerHz = 10.0f;

    /** How many round trips the string must make after the excitation before
        it may hold. The loop low-pass sheds the pluck's top end at a rate per
        round trip, so this is counted in periods, not in seconds: after this
        many, everything above the Brightness cutoff has gone and what is left
        is the string's own partials. Holding sooner would freeze the raw
        excitation in a loop that no longer damps it, which buzzes. */
    constexpr int minimumPeriodsBeforeHold = 24;

    /** Below this string RMS (-120 dBFS) a voice can be freed. */
    constexpr float silenceThreshold = 1.0e-6f;

    /** A voice louder than this inside the loop has run away (a working
        string stays below about 2). */
    constexpr float runawayLevel = 16.0f;

    /** Headroom so that full-velocity chords don't clip before the master gain. */
    constexpr float voiceGainTrim = 0.5f;

    /** Below this note the loop low-pass is split into a cascade, so that a
        low string loses its top end as fast in seconds as this one does. The
        loop filter acts once per round trip, and a note an octave down makes
        half as many trips a second: without this, a 20 Hz string kept its
        highs eight times longer than a 160 Hz one and stayed a gritty buzz
        of clicks for seconds (measured: 1.4 % of its energy still above
        2 kHz after 1.25 s, against none at E3). */
    constexpr float brightnessReferenceHz = 130.81f;   // C3
    constexpr int   maximumLoopStages     = 16;

    /** While a note holds, the loop low-pass eases off to a much lighter one:
        this much loss per second at the Brightness cutoff, far less below it,
        more above. Light enough that the held level stays where Sustain puts
        it; enough that no corner of the pluck is frozen into the loop for
        ever. It glides there, and back when the key is let go, over this
        long, because a change of filter moves the loop delay and a sudden
        retune clicks. */
    constexpr float holdLossDbPerSecond = 2.0f;
    constexpr float holdBlendSeconds    = 0.15f;
    constexpr int   holdUpdateInterval  = 32;

    /** The coefficient of a one-pole low-pass that loses lossDb at w. */
    inline float onePoleCoeffForLoss (float w, float lossDb) noexcept
    {
        const auto g2 = std::pow (10.0f, -lossDb / 10.0f);
        const auto p = 1.0f - g2 * std::cos (w), q = 1.0f - g2;
        if (q <= 1.0e-7f)
            return 0.0f;   // no loss at all: a wire
        // Solve (1 - a)^2 = g2 (1 - 2 a cos w + a^2) for the root below 1.
        return juce::jlimit (0.0f, 0.9999f, (p - std::sqrt (juce::jmax (0.0f, p * p - q * q))) / q);
    }

    /** How often the string is retuned while gliding, in samples. */
    constexpr int glideUpdateInterval = 32;


    /** A sine or a square at f0 lands every bit of its energy on the string's
        own partials, so it comes out louder than a noise burst of the same
        drive, and more so the higher the note. Measured with a K-weighted
        loudness over the first half second, against the noise burst, across
        E1 to E7 and attacks of 3 to 50 ms, the square was 8 dB louder at
        A#3 and the sine 5 dB, each rising by the slope per semitone. Each
        is brought down by that much so the whole Tone knob plays at the
        noise burst's level, which is left exactly as it was. */
    constexpr float sineExcessDb           = 5.0f;
    constexpr float sineExcessDbPerNote    = 0.144f;
    constexpr float squareExcessDb         = 8.0f;
    constexpr float squareExcessDbPerNote  = 0.095f;
    constexpr float toneReferenceNote      = 58.0f;

    constexpr float twoPi = juce::MathConstants<float>::twoPi;

} // namespace pluck::voice
