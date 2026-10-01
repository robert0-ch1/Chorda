/*
  ==============================================================================

    KarplusVoice.h

    One voice of an extended Karplus-Strong string model.

    exciter -> pick comb -> (+) -> delay + allpass -> loop LP -> loop HP --+
                             ^                                             |
                             +-------------- x feedback <------------------+
                                                                           |
    (excitation + loop out) -> pickup comb -> damper x make-up -> (+) -> DC blocker -> x velocity -> out
                                                                   ^
                                       sub sine (f0 / 2) x RMS ----+

    There is no amplitude envelope: decay, sustain and release set the loop
    feedback gain, derived from T60. The loop high-pass removes DC so the
    feedback may exceed 1 to make up the low-pass loss at f0; the gain is
    capped so the loop stays below unity at every resonance. Hold raises the
    feedback to that cap and eases the low-pass to a light setting.

    The damper sits after the loop, so it changes timbre without altering the
    decay envelope.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <complex>
#include <vector>
#include "../Parameters.h"

namespace pluck
{

//==============================================================================
/** Per-block snapshot of the parameters a voice needs. Filled by the
    processor, so voices never access the APVTS directly. */
struct VoiceParameters
{
    float exciterTone       = toneNoise; ///< 0..1: sine -> square -> noise burst
    float attackSeconds     = 0.003f;    ///< length of the excitation
    float pickPosition      = 0.2f;      ///< fraction of the string length, never zero
    float decaySeconds      = 2.0f;      ///< T60 while held
    float sustainLevel      = 0.0f;      ///< 0..1, see sustainRangeDb in Parameters.h
    float releaseSeconds    = 0.3f;      ///< T60 after key release
    float brightnessHz      = 5000.0f;   ///< cutoff of the loop low-pass
    float damperPosition    = 0.0f;      ///< 0 = off, else fraction of string length
    float damperPressure    = 0.6f;      ///< 0..1, depth of the damper filter
    float subLevel          = 0.0f;      ///< 0..1 level of the sub sine
    int   transposeSemitones = 0;        ///< transpose, in semitones
    float modWheel          = 0.0f;      ///< 0..1, raises brightness
    float glideSeconds      = 0.0f;      ///< portamento time from the previous note

    /** Per-sample damper position from the LFO (fraction of string length),
        or null when idle. Owned by the processor. */
    const float* damperModulation = nullptr;

    /** Per-sample damper pressure from the LFO, or null when idle. */
    const float* pressureModulation = nullptr;
};

//==============================================================================
class KarplusVoice final : public juce::SynthesiserVoice
{
public:
    KarplusVoice();

    /** Called once per block before rendering. Real-time safe. */
    void setParameters (const VoiceParameters& newParams) noexcept;

    /** True after note-off while the string decays at the release rate. */
    bool isReleasing() const noexcept   { return stage == Stage::releasing; }

    //==============================================================================
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound* sound, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void setCurrentPlaybackSampleRate (double newRate) override;
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;
    using juce::SynthesiserVoice::renderNextBlock;

private:
    //==============================================================================
    /** Note stage. Stages differ only in loop damping. */
    enum class Stage { idle, exciting, decaying, holding, releasing };

    float renderSample() noexcept;
    void  periodCompleted() noexcept;
    float nextExciterSample() noexcept;
    float readDelayLine() noexcept;
    void  updateDelayInterpolator() noexcept;

    void  updateTuning() noexcept;
    void  updateLoopFilters() noexcept;
    std::complex<float> smoothLoopResponse (float frequencyHz) const noexcept;
    float highestResonanceGain() const noexcept;
    float feedbackForT60 (float seconds) const noexcept;
    void  updateFeedbackTarget() noexcept;
    void  setStage (Stage newStage) noexcept;

    /** Advances the glide by `samples` and retunes. */
    void  advanceGlide (int samples) noexcept;

public:
    /** Legato: retunes to another note without replucking. */
    void  slideToNote (int midiNoteNumber) noexcept;

    /** Starts the current note gliding from `hz`. */
    void  glideFromFrequency (float hz) noexcept;
    void  glideFromFrequency (float hz, float seconds) noexcept;

    float getCurrentFrequency() const noexcept   { return frequencyHz; }

private:

    /** Sets up the exciter for the current pitch: filter, drives, trims, pick comb. */
    void  prepareExcitation() noexcept;

    /** Per-waveform exciter drive for the current pitch. */
    void  updateExcitationDrive() noexcept;

    /** Exciter band-limiting filter and its noise make-up gain. */
    void  prepareExciterFilter() noexcept;

public:
    /** Impulse-response energy of `stages` identical one-pole low-passes with coefficient a. */
    static double cascadeEnergy (int stages, double a) noexcept;
private:

    /** Computes the Tone crossfade gains for sine, square and noise. */
    void  updateExciterMix() noexcept;

    /** Damper position including LFO modulation. */
    float currentDamperPosition() const noexcept;

    /** Damper pressure including LFO modulation. */
    float currentDamperPressure() const noexcept;

    /** Damper comb delay in samples for a position, 0 when off. */
    float damperDelayFor (float position) const noexcept;

    /** Updates the damper and pickup targets from position and pressure. */
    void  updateDamperFilter() noexcept;

    /** Glides damper delay and depth one sample towards target (or jumps,
        with snap) and recomputes the tap weights. */
    void  glideDamper (bool snap) noexcept;

    /** Pickup comb, damper and level make-up for one sample. */
    float applyDamper (float input) noexcept;
    float readDamperHistory (float delay) const noexcept;
    static float readHistory (const std::vector<float>& history, int writeIndex, float delay) noexcept;



    /** Actual loop period at f0 in samples, including allpass and filter phase delay. */
    float actualPeriodSamples() const noexcept;

    /** Response of a single loop low-pass stage at `hz`. */
    std::complex<float> lowPassResponse (float hz) const noexcept;

    /** Response of the full low-pass cascade at z = e^(-jw). */
    std::complex<float> loopLowPassResponse (std::complex<float> z) const noexcept;
    void  clearBuffers() noexcept;
    void  endVoice() noexcept;

    //==============================================================================
    VoiceParameters params;
    Stage stage = Stage::idle;

    // Glide: exponential in frequency, i.e. linear in semitones.
    int   soundingNote       = 60;       ///< note the string is tuned to; legato changes it
    bool  gliding            = false;
    float glideFrequencyHz   = 440.0f;   ///< current glide frequency
    float glideStartLog      = 0.0f;     ///< log2 of where the glide began
    float glideTargetLog     = 0.0f;
    int   glideSamplesTotal  = 0;
    int   glideSamplesLeft   = 0;
    int   glideUpdateCounter = 0;

    // Note state
    float pitchBend       = 0.0f;   ///< -1..1
    float targetFrequencyHz   = 440.0f;  ///< frequency from note, bend and transpose
    float frequencyHz     = 440.0f;
    float periodSamples   = 100.0f;
    float velocityGain    = 0.0f;
    float velocityBrightness = 1.0f; ///< cutoff multiplier from velocity

    // Exciter
    juce::Random random;

    // Tone crossfade gains, set per note. Each is exactly 0 or 1 at the knob ends.
    float exciterSineGain    = 0.0f;
    float exciterSquareGain  = 0.0f;
    float exciterNoiseGain   = 1.0f;

    // Drive per waveform class: tonal input sums coherently over the attack,
    // noise sums in power, and the band-limit attenuates each differently.
    float exciterTonalDrive  = 1.0f;
    float exciterNoiseDrive  = 1.0f;

    int   exciterStages       = 1;      ///< exciter low-pass stages, matches loopStages
    float exciterStageCoeff   = 0.5f;
    float exciterNoiseMakeup  = 1.0f;   ///< restores noise power after the cascade
    std::array<float, 16> exciterStageState {};
    float exciterPhase       = 0.0f;
    int   attackSamplesLeft  = 0;
    int   attackSamplesTotal = 1;
    int   combTailLeft       = 0;       ///< samples of comb output left after the attack
    int   combDelaySamples   = 0;       ///< 0 = comb bypassed
    std::vector<float> exciterHistory;  ///< ring buffer feeding the pick-position comb

    // Faded tail of a cut-off note, mixed under the next one
    std::vector<float> cutTail, scratchTail;
    int   cutTailRead        = 0;
    int   cutTailLeft        = 0;
    void  captureCutTail() noexcept;
    void  mixCutTail (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept;
    int   exciterWriteIndex  = 0;
    bool  exciterFinished    = true;

    // String
    std::vector<float> delayLine;
    int   writeIndex          = 0;
    float loopDelaySamples    = 100.0f; ///< fractional delay, compensated for filter phase delay
    float loopDelayTarget     = 100.0f; ///< glide target for loopDelaySamples
    float loopDelayGlideCoeff = 0.01f;
    bool  snapLoopDelay       = true;   ///< jump to target at note-on

    // Fractional delay: integer tap plus first-order allpass, flat in magnitude.
    int   integerDelay        = 99;
    float allpassCoeff        = 0.0f;
    float allpassX1           = 0.0f, allpassY1 = 0.0f;

    // Loop filters: Brightness low-pass, and a high-pass well below f0 that
    // makes feedback above 1 safe.
    float lowPassCoeff        = 0.5f;   ///< 'a' of y[n] = (1-a) x[n] + a y[n-1]
    int   loopStages          = 1;      ///< low-pass cascade length, more on low notes
    float holdBlend           = 0.0f;   ///< 0 = Brightness low-pass, 1 = hold low-pass
    int   holdUpdateCounter   = 0;
    float loopStageCoeff      = 0.5f;   ///< 'a' of each stage
    std::array<float, 16> loopStageState {};
    float subLevelCoeff       = 0.01f;  ///< sub level follower coefficient
    float highPassCutoffHz    = 2.0f;
    float highPassCoeff       = 0.999f; ///< 'R' of y[n] = x[n] - x[n-1] + R y[n-1]
    float highPassIn1         = 0.0f, highPassOut1 = 0.0f;

    // Damper: ((1 - m) + m z^-D)^N as one FIR with N + 1 taps at multiples of
    // D. D is fractional (linear interpolation) so moving notches stay smooth.
    float damperMix           = 0.0f;   ///< m, from pressure; 0 = off
    float damperDelay         = 0.0f;   ///< D in samples
    float damperMixTarget     = 0.0f, damperDelayTarget = 0.0f;   ///< glide targets
    float damperGlideCoeff    = 0.01f;
    float damperModulated     = 0.0f;   ///< LFO position, fraction of string length
    float pressureModulated   = 0.0f;   ///< LFO pressure
    std::array<float, 7> damperWeights {};   ///< binomial tap weights, N = 6
    std::vector<float> damperHistory;   ///< pickup output history

    // Pickup comb at the pick position.
    std::vector<float> listenHistory;   ///< string output history
    int   listenWriteIndex    = 0;
    float listenDelay         = 1.0f, listenDelayTarget = 1.0f;   ///< pick position x period, in samples
    int   damperWriteIndex    = 0;

    // Damper make-up: input and output mean squares and the gain matching them.
    float damperLevelCoeff    = 0.001f;
    float damperLevelIn       = 0.0f, damperLevelOut = 0.0f;
    float damperMakeup        = 1.0f, damperMakeupTarget = 1.0f;
    int   damperMakeupCounter = 0;

    float loopFilterGainAtF0  = 1.0f;   ///< |H(f0)| of the loop filters
    float feedbackCap         = 0.999f; ///< feedback limit for loop gain < 1

    // Damping, i.e. the envelope
    float feedbackGain        = 0.99f;  ///< per-period gain, smoothed towards feedbackTarget
    float feedbackTarget      = 0.99f;
    float feedbackSmoothing   = 0.01f;  ///< per-sample coefficient, one-period time constant

    // Level tracking over whole periods: a short pluck on a low string is a
    // travelling pulse. RMS, since a dispersing pulse loses peak but not energy.
    int   periodCounter          = 0;
    float periodEnergy           = 0.0f;   ///< sum of squares in the current period
    float lastPeriodRms          = 0.0f;   ///< RMS of the last completed period
    int   periodsSinceExcitation = 0;      ///< completed periods since the exciter stopped
    float referenceLevel         = 0.0f;   ///< string RMS once the excitation has circulated
    bool  stringIsSilent         = false;  ///< set by periodCompleted(), acted on after the block

    // Sub oscillator: sine at f0 / 2 scaled by string level
    float subPhase        = 0.0f;
    float subPhaseStep     = 0.0f;  ///< from the actual loop period, so it does not drift
    float subMeanSquare    = 0.0f;  ///< running mean square of the string

    // Output conditioning
    float dcBlockerCoeff = 0.995f;
    float dcIn1 = 0.0f, dcOut1 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KarplusVoice)
};

} // namespace pluck
