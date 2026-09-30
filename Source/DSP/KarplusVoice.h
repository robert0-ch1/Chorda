/*
  ==============================================================================

    KarplusVoice.h

    One voice of the extended Karplus-Strong string model.

    Signal flow per sample:

        exciter -> pick comb -> (+) -> delay line -> loop low-pass -> loop high-pass -+
                                 ^                                                     |
                                 +------------------ x feedback <---------------------+
                                                                                       |
                                                        damper filter x level make-up -+
                                                                                       |
                        sub sine (f0 / 2) x string RMS --(+)-> DC blocker -> x velocity -> out

    The delay line plus the loop filters form a resonator whose period sets the
    pitch. The exciter feeds energy in over the "attack" time, and the
    feedback gain (the string's damping) is what shapes the note afterwards:

        decay    : T60 of the string while the key is held
        sustain  : level at which damping is switched off so the string
                   keeps ringing (detected on the string's own RMS per period)
        release  : T60 of the string once the key is released

    There is no separate amplitude envelope: every stage is a change of loss
    inside the loop, so the sound always behaves like a physical string.

    Two more controls:

        damper : a finger touching the string at some position. It sits
                 after the string, not in it: a cascade of combs that keeps
                 the partials with a node at the finger and cuts the others,
                 as deeply as the pressure says, with a make-up gain that
                 keeps the level where it was. So it changes the colour of
                 the note and nothing else: the envelope is exactly the
                 envelope. Two LFOs can move its position and its pressure
        sub    : a sine an octave below the note whose level follows the
                 string's own RMS, so it decays, holds and releases with it

    Why a high-pass in the loop: the low-pass ("Brightness") also eats some
    of the fundamental on every round trip, and the feedback gain
    compensates that and may exceed 1.0. That would make DC grow without
    bound, so a gentle high-pass sits in the loop too, and the gain is capped
    by the peak of the low-pass / high-pass response wherever a resonance can
    sit, so the loop gain stays below 1 whatever the comb does to the
    resonance positions. In the "hold" stage the loop gain rises to that cap,
    which makes up the loss at f0 exactly, and the low-pass eases off to a
    very light one (a few dB a second at the Brightness cutoff), so the level
    holds where Sustain says and no corner of the pluck is frozen in. (The
    low-pass used to step aside entirely while holding; that froze the pluck's
    remaining top end into a lossless loop and ticked once a period, and the
    sudden change of loop delay clicked as the hold began.)

    The class derives from juce::SynthesiserVoice so that juce::Synthesiser
    handles note tracking, voice stealing, sustain pedal and sample-accurate
    MIDI timing for us.

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
/** Snapshot of everything a voice needs from the parameter tree.

    The processor fills one of these once per block from the atomic parameter
    values and hands it to every voice. Voices never touch the APVTS directly,
    which keeps them easy to test in isolation.
*/
struct VoiceParameters
{
    float exciterTone       = toneNoise; ///< 0..1: sine -> square -> noise burst
    float attackSeconds     = 0.003f;    ///< length of the excitation
    float pickPosition      = 0.2f;      ///< fraction of the string length, never zero: a string is always picked somewhere
    float decaySeconds      = 2.0f;      ///< T60 while held
    float sustainLevel      = 0.0f;      ///< 0..1, see sustainRangeDb in Parameters.h
    float releaseSeconds    = 0.3f;      ///< T60 after key release
    float brightnessHz      = 5000.0f;   ///< cutoff of the loop low-pass
    float damperPosition    = 0.0f;      ///< 0 = off, otherwise fraction of string length
    float damperPressure    = 0.6f;      ///< 0..1, depth of the damper filter
    float subLevel          = 0.0f;      ///< 0..1 level of the sub sine
    int   transposeSemitones = 0;        ///< octave transposer
    float modWheel          = 0.0f;      ///< 0..1, raises brightness
    float glideSeconds      = 0.0f;      ///< time to slide from the last note to this one

    /** Where the position LFO puts the damper for every sample of the block,
        as a fraction of the string length, or null while it is idle. The
        processor owns the buffer and refills it before each block. */
    const float* damperModulation = nullptr;

    /** The damper pressure the pressure LFO asks for, sample by sample, or
        null while it is idle. */
    const float* pressureModulation = nullptr;
};

//==============================================================================
class KarplusVoice final : public juce::SynthesiserVoice
{
public:
    KarplusVoice();

    /** Called by the processor once per block, before rendering. Real-time safe. */
    void setParameters (const VoiceParameters& newParams) noexcept;

    /** True once the key is up and the string is being damped at the release rate. */
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

private:
    //==============================================================================
    /** Where the string is in its life. Only the damping differs between stages. */
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

    /** Moves the glide on, and retunes the string as it goes. */
    void  advanceGlide (int samples) noexcept;

public:
    /** Slides this voice to another note without replucking it: legato. */
    void  slideToNote (int midiNoteNumber) noexcept;

    /** Starts the next note from this pitch rather than at its own. */
    void  glideFromFrequency (float hz) noexcept;
    void  glideFromFrequency (float hz, float seconds) noexcept;

    float getCurrentFrequency() const noexcept   { return frequencyHz; }

private:

    /** Sets up the pluck for the pitch the string is at: drives, trims, pick comb. */
    void  prepareExcitation() noexcept;

    /** The per-period drive of each waveform, for the current pitch. */
    void  updateExcitationDrive() noexcept;

    /** The pluck's band-limiting filter, and the noise make-up that goes with it. */
    void  prepareExciterFilter() noexcept;

    /** Works out the Tone crossfade between the three waveforms. */
    void  updateExciterMix() noexcept;

    /** Where the finger is right now: the Damper Position, moved by the LFO. */
    float currentDamperPosition() const noexcept;

    /** The pressure right now: the Damper Pressure, moved by its LFO. */
    float currentDamperPressure() const noexcept;

    /** The comb delay for a damper position, 0 when it is off. */
    float damperDelayFor (float position) const noexcept;

    /** Works out the damper's delay and tap weights from where the finger
        is and how hard it presses. */
    void  updateDamperFilter() noexcept;

    /** Moves the damper's delay and depth one sample towards where it is
        set (or straight there, with snap), and works out its tap weights. */
    void  glideDamper (bool snap) noexcept;

    /** The damper filter and its make-up gain, one sample. */
    float applyDamper (float input) noexcept;
    float readDamperHistory (float delay) const noexcept;
    static float readHistory (const std::vector<float>& history, int writeIndex, float delay) noexcept;



    /** The loop's true period in samples at f0, allpass and filters included.
        The nominal period is what we ask for; this is what we get. */
    float actualPeriodSamples() const noexcept;

    /** The loop low-pass on its own, at one frequency. */
    std::complex<float> lowPassResponse (float hz) const noexcept;

    /** The loop's low-pass, every stage of it, at z = e^(-jw). */
    std::complex<float> loopLowPassResponse (std::complex<float> z) const noexcept;
    void  clearBuffers() noexcept;
    void  endVoice() noexcept;

    //==============================================================================
    VoiceParameters params;
    Stage stage = Stage::idle;

    // Glide: the string is retuned towards the new note over glideSeconds
    // rather than jumping to it. The pitch moves at a constant rate in
    // semitones, which is what a finger sliding along a string does.
    int   soundingNote       = 60;       ///< the note the string is tuned to: legato moves it, the key does not
    bool  gliding            = false;
    float glideFrequencyHz   = 440.0f;   ///< where the string is now
    float glideStartLog      = 0.0f;     ///< log2 of where the glide began
    float glideTargetLog     = 0.0f;
    int   glideSamplesTotal  = 0;
    int   glideSamplesLeft   = 0;
    int   glideUpdateCounter = 0;

    // Note state
    float pitchBend       = 0.0f;   ///< -1..1
    float targetFrequencyHz   = 440.0f;  ///< where the note says the string should be
    float frequencyHz     = 440.0f;
    float periodSamples   = 100.0f;
    float velocityGain    = 0.0f;
    float velocityBrightness = 1.0f; ///< brightness factor from velocity (hard = brighter)

    // Exciter
    juce::Random random;

    // The Tone crossfade, worked out once per note. At either end of the knob
    // one of these is 1 and the others 0, so each waveform is exactly what it
    // always was; in between they are mixed.
    float exciterSineGain    = 0.0f;
    float exciterSquareGain  = 0.0f;
    float exciterNoiseGain   = 1.0f;

    // How hard each kind of waveform drives the string. They differ because a
    // waveform at f0 adds up in step over a long excitation while noise adds up
    // in power, and because the band-limiting costs each of them a different
    // amount. Each part of the Tone mix carries its own, so the crossfade never
    // bulges in the middle and either end is exactly what it always was.
    float exciterTonalDrive  = 1.0f;
    float exciterNoiseDrive  = 1.0f;

    int   exciterStages       = 1;      ///< the pluck's band-limit: as many stages as the loop has
    float exciterStageCoeff   = 0.5f;
    float exciterNoiseMakeup  = 1.0f;   ///< equal noise power through those stages
    std::array<float, 16> exciterStageState {};
    float exciterPhase       = 0.0f;
    int   attackSamplesLeft  = 0;
    int   attackSamplesTotal = 1;
    int   combTailLeft       = 0;       ///< the comb keeps emitting for combDelay samples after the excitation
    int   combDelaySamples   = 0;       ///< 0 = comb bypassed
    std::vector<float> exciterHistory;  ///< ring buffer feeding the pick-position comb

    // The fade-out of a string that was cut off, mixed in under the next note
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
    float loopDelaySamples    = 100.0f; ///< fractional read offset, already compensated for filter delay
    float loopDelayTarget     = 100.0f; ///< where the tuning wants it; loopDelaySamples glides there
    float loopDelayGlideCoeff = 0.01f;
    bool  snapLoopDelay       = true;   ///< set while a note starts: no glide from the last note's length

    // Fractional delay: an integer tap plus a first-order allpass, which
    // (unlike linear interpolation) loses nothing at high frequencies.
    int   integerDelay        = 99;
    float allpassCoeff        = 0.0f;
    float allpassX1           = 0.0f, allpassY1 = 0.0f;

    // Loop filters. The low-pass is "Brightness"; the high-pass is a long way
    // below f0 and makes a feedback gain above 1 safe.
    float lowPassCoeff        = 0.5f;   ///< 'a' of y[n] = (1-a) x[n] + a y[n-1]
    int   loopStages          = 1;      ///< the loop low-pass as a cascade: more stages on lower notes
    float holdBlend           = 0.0f;   ///< 0 = the Brightness low-pass, 1 = the light one used while holding
    int   holdUpdateCounter   = 0;
    float loopStageCoeff      = 0.5f;   ///< 'a' of each stage
    std::array<float, 16> loopStageState {};
    float subLevelCoeff       = 0.01f;  ///< one-pole coefficient of the sub's level follower
    float highPassCutoffHz    = 2.0f;
    float highPassCoeff       = 0.999f; ///< 'R' of y[n] = x[n] - x[n-1] + R y[n-1]
    float highPassIn1         = 0.0f, highPassOut1 = 0.0f;

    // Damper: ((1 - m) + m z^-D)^N after the string, as one FIR with N + 1
    // taps at multiples of D. D is fractional and read by linear
    // interpolation, so a moving finger slides the notches instead of
    // stepping them from sample to sample, which would click.
    float damperMix           = 0.0f;   ///< m, from the pressure; 0 = off
    float damperDelay         = 0.0f;   ///< D in samples
    float damperMixTarget     = 0.0f, damperDelayTarget = 0.0f;   ///< where they glide to
    float damperGlideCoeff    = 0.01f;
    float damperModulated     = 0.0f;   ///< where the position LFO has the finger, a fraction of the string
    float pressureModulated   = 0.0f;   ///< how hard the pressure LFO has it pressing
    std::array<float, 7> damperWeights {};   ///< the binomial tap weights, N = 6
    std::vector<float> damperHistory;   ///< ring buffer of the string as heard at the pick

    // Where the string is heard from: the pick, as a pickup there would.
    std::vector<float> listenHistory;   ///< ring buffer of the string itself
    int   listenWriteIndex    = 0;
    float listenDelay         = 1.0f, listenDelayTarget = 1.0f;   ///< pick position x period, in samples
    int   damperWriteIndex    = 0;

    // The damper's level make-up: mean squares of the string going in and
    // of the filtered sound coming out, and the gain that evens them.
    float damperLevelCoeff    = 0.001f;
    float damperLevelIn       = 0.0f, damperLevelOut = 0.0f;
    float damperMakeup        = 1.0f, damperMakeupTarget = 1.0f;
    int   damperMakeupCounter = 0;

    float loopFilterGainAtF0  = 1.0f;   ///< |H(f0)| of everything in the loop, compensated by the feedback gain
    float feedbackCap         = 0.999f; ///< keeps the loop gain below 1 at every resonance

    // Damping (the "envelope")
    float feedbackGain        = 0.99f;  ///< current per-period gain, smoothed towards feedbackTarget
    float feedbackTarget      = 0.99f;
    float feedbackSmoothing   = 0.01f;  ///< per-sample coefficient, one-period time constant

    // Level tracking. A short pluck on a low string is a pulse travelling round
    // the loop, so the output is near zero between passes; the only honest
    // measure of the string's level is taken over one whole period. RMS rather
    // than peak, because a pulse spreading out loses peak but not energy.
    int   periodCounter          = 0;
    float periodEnergy           = 0.0f;   ///< sum of squares in the current period
    float lastPeriodRms          = 0.0f;   ///< RMS of the last completed period
    int   periodsSinceExcitation = 0;      ///< completed periods since the exciter stopped
    float referenceLevel         = 0.0f;   ///< string RMS once the excitation has circulated
    bool  stringIsSilent         = false;  ///< set by periodCompleted(), acted on after the block

    // Sub oscillator: a sine an octave down riding on the string's level
    float subPhase        = 0.0f;
    float subPhaseStep     = 0.0f;  ///< half a cycle of the string's true period, so the sub cannot drift
    float subMeanSquare    = 0.0f;  ///< continuous mean square of the string, the sub's level

    // Output conditioning
    float dcBlockerCoeff = 0.995f;
    float dcIn1 = 0.0f, dcOut1 = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KarplusVoice)
};

} // namespace pluck
