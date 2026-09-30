/*
  ==============================================================================

    KarplusVoice.cpp

    See KarplusVoice.h for the block diagram. Everything in this file runs on
    the audio thread: no allocation, no locks, no logging.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "KarplusSound.h"

namespace pluck
{

namespace
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
    float onePoleCoeffForLoss (float w, float lossDb) noexcept
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
}

//==============================================================================
KarplusVoice::KarplusVoice()
{
    // Give the buffers a sensible size straight away; the synthesiser will
    // call setCurrentPlaybackSampleRate() again with the real rate.
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
    damperHistory.assign (capacity * (size_t) damperStages / 2 + 8, 0.0f);   // the longest tap: N x half a period
    listenHistory.assign (capacity * (size_t) (damperStages / 2 + 2) + 8, 0.0f); // a period, plus the reference behind both filters
    damperLevelCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (damperLevelSeconds * sr));
    damperGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (damperGlideSeconds * sr)));
    loopDelayGlideCoeff = juce::jlimit (1.0e-5f, 1.0f, 1.0f - std::exp (-1.0f / (loopDelayGlideSeconds * sr)));

    // One-pole high-pass: R = 1 - 2*pi*fc/fs is accurate enough at 10 Hz.
    dcBlockerCoeff = 1.0f - twoPi * dcBlockerHz / sr;
    cutTail.assign ((size_t) juce::jmax (1, juce::roundToInt (cutFadeSeconds * sr)), 0.0f);
    scratchTail.assign (cutTail.size(), 0.0f);
    cutTailRead = cutTailLeft = 0;
    subLevelCoeff  = juce::jlimit (1.0e-5f, 1.0f, 1.0f / (subLevelSeconds * sr));

    // A note that was sounding at the old rate has meaningless buffer contents now.
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

    // Everything about the string can be tweaked while a note rings. The
    // exciter settings, in contrast, are only read at note-on.
    if (stage != Stage::idle && stringChanged)
    {
        updateTuning();
        updateFeedbackTarget();
    }

}

//==============================================================================
bool KarplusVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<KarplusSound*> (sound) != nullptr;
}

void KarplusVoice::startNote (int midiNoteNumber, float velocity,
                              juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    soundingNote = midiNoteNumber;   // legato slides this on without a new note-on
    holdBlend = 0.0f;
    holdUpdateCounter = 0;

    stage           = Stage::exciting;
    velocityGain    = velocity * voiceGainTrim;
    pitchBend       = (float) (currentPitchWheelPosition - 8192) / 8192.0f;

    // Velocity also decides how the string is plucked: hard is shorter and brighter.
    velocityBrightness = juce::jmap (velocity, velocityBrightnessSoft, velocityBrightnessHard);
    const auto attackFactor = juce::jmap (velocity, velocityAttackFactorSoft, velocityAttackFactorHard);

    // A fresh note must never hear what the previous one left in the loop.
    clearBuffers();
    damperDelay = 0.0f;           // a new note starts with the damper where it is, not gliding from the last one
    snapLoopDelay = true;         // and with its string the right length at once
    updateTuning();
    snapLoopDelay = false;
    glideDamper (true);

    // Exciter: energy is fed in for "attack" seconds under a raised-cosine
    // window. A few milliseconds is a pluck; hundreds of milliseconds is a bow.
    const auto sr = (float) getSampleRate();
    attackSamplesTotal = juce::jmax (2, juce::roundToInt (params.attackSeconds * attackFactor * sr));
    attackSamplesLeft  = attackSamplesTotal;
    exciterPhase       = 0.0f;
    exciterFinished    = false;

    prepareExcitation();

    // Damping starts at the "decay" rate and is smoothed over one period, so a
    // change of stage never steps the level.
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

    // Equal noise power through it. One stage has a closed form; a cascade
    // is summed from its impulse response.
    if (exciterStages == 1)
    {
        exciterNoiseMakeup = std::sqrt ((1.0f + exciterStageCoeff) / (1.0f - exciterStageCoeff));
        return;
    }

    std::array<double, 16> state {};
    double energy = 0.0;
    const auto a = (double) exciterStageCoeff;
    const auto length = juce::jlimit (64, 40000, (int) (30.0 * exciterStages / juce::jmax (1.0e-4, 1.0 - a)));
    for (int n = 0; n < length; ++n)
    {
        double x = n == 0 ? 1.0 : 0.0;
        for (int k = 0; k < exciterStages; ++k)
        {
            state[(size_t) k] = (1.0 - a) * x + a * state[(size_t) k];
            x = state[(size_t) k];
        }
        energy += x * x;
    }
    exciterNoiseMakeup = (float) (1.0 / std::sqrt (juce::jmax (1.0e-12, energy)));
}

void KarplusVoice::glideFromFrequency (float hz) noexcept
{
    glideFromFrequency (hz, params.glideSeconds);
}

void KarplusVoice::glideFromFrequency (float hz, float seconds) noexcept
{
    // Called just after the note starts: the string is retuned to where the
    // last note was and slides from there to this one.
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

    // The string is plucked at the pitch it starts from. Its pluck was set up
    // for the note it is heading to, which made a glide down up to 4 dB too
    // loud and a glide up up to 6 dB too quiet: set it up again here, while
    // nothing has been played yet.
    if (stage == Stage::exciting && attackSamplesLeft == attackSamplesTotal)
    {
        prepareExcitation();
        feedbackSmoothing = juce::jlimit (0.001f, 1.0f, 1.0f / periodSamples);
        feedbackGain = feedbackTarget;
    }
}

void KarplusVoice::slideToNote (int midiNoteNumber) noexcept
{
    // Legato: the string keeps ringing and is retuned to the new note, gliding
    // there over Glide, and never quicker than a click-free slide.
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
        // Normal note-off: damp the string at the "release" rate. The voice
        // frees itself in renderNextBlock() once the string has died away.
        setStage (Stage::releasing);
    }
    else
    {
        // Voice stealing or an "all notes off": stop now, but fade out.
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

//==============================================================================
void KarplusVoice::captureCutTail() noexcept
{
    if (stage == Stage::idle || cutTail.empty())
        return;

    // Play the string on for a few milliseconds, now, under a fade, and keep
    // that to be mixed in while the next note starts from a clean string.
    // Whatever is left of an earlier tail is folded in, so two quick cuts in
    // a row still fade.
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
        // The LFOs move the finger along the string and press it harder and
        // softer, and the filter follows them every sample.
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

        // Easing the loop filter into and out of the hold, in small steps.
        const auto holdTarget = stage == Stage::holding ? 1.0f : 0.0f;
        if (! juce::exactlyEqual (holdBlend, holdTarget) && ++holdUpdateCounter >= holdUpdateInterval)
        {
            holdUpdateCounter = 0;
            const auto step = (float) holdUpdateInterval / (holdBlendSeconds * (float) getSampleRate());
            holdBlend = holdTarget > holdBlend ? juce::jmin (holdTarget, holdBlend + step) : juce::jmax (holdTarget, holdBlend - step);
            updateTuning();
            updateFeedbackTarget();
        }

        // Retuning is far too heavy to do every sample, and a glide does not
        // need it: a thirty-second of a millisecond is finer than the ear.
        if (gliding && ++glideUpdateCounter >= glideUpdateInterval)
        {
            glideUpdateCounter = 0;
            advanceGlide (glideUpdateInterval);
        }
    }

    // Free the voice once the string has decayed to silence (decided per
    // period in periodCompleted()), whether that is the end of a release or a
    // short decay with the key still held. Freeing early keeps voices
    // available for new notes instead of hoarding them.
    if (stringIsSilent)
        endVoice();
}

float KarplusVoice::renderSample() noexcept
{
    const auto excitation = exciterFinished ? 0.0f : nextExciterSample();

    // Glide the loop length towards where the tuning wants it.
    if (! juce::exactlyEqual (loopDelaySamples, loopDelayTarget))
    {
        loopDelaySamples += (loopDelayTarget - loopDelaySamples) * loopDelayGlideCoeff;
        if (std::abs (loopDelayTarget - loopDelaySamples) < 1.0e-4f)
            loopDelaySamples = loopDelayTarget;
        updateDelayInterpolator();
    }

    // Read the string one period ago (fractional).
    const auto signal = readDelayLine();

    // Loop low-pass: high partials lose energy on every round trip, exactly
    // as they do in a real string. This is what "Brightness" controls, and it
    // stays in the loop at every stage: a string that stopped losing its highs
    // would ring on whatever noise it was given, for ever.
    auto smoothed = signal;
    for (int k = 0; k < loopStages; ++k)
    {
        auto& state = loopStageState[(size_t) k];
        state = (1.0f - loopStageCoeff) * smoothed + loopStageCoeff * state;
        smoothed = state;
    }
    const auto lowPassed = smoothed;

    // Loop high-pass: removes the DC mode so the feedback may exceed 1.
    const auto highPassed = lowPassed - highPassIn1 + highPassCoeff * highPassOut1;
    highPassIn1  = lowPassed;
    highPassOut1 = highPassed;

    // The damper is not in the loop any more: the string rings exactly as
    // the envelope says, and the damper shapes what you hear of it (below).
    const auto filtered = highPassed;

    // Damping: glide towards the current stage's feedback gain.
    feedbackGain += (feedbackTarget - feedbackGain) * feedbackSmoothing;

    // Never above the stability cap, even while gliding towards a lower
    // target: after a retune (a bend, a glide, an octave jump) the cap can
    // drop at once while the smoothed gain is still up at the old value, and
    // a loop gain over 1 for a few periods is all a runaway needs.
    feedbackGain = juce::jmin (feedbackGain, feedbackCap);

    // Feed the string back into itself plus any new excitation.
    const auto string = excitation + feedbackGain * filtered;
    delayLine[(size_t) writeIndex] = string;

    if (++writeIndex >= (int) delayLine.size())
        writeIndex = 0;

    // Accumulate energy over each period; stage decisions happen once per period.
    periodEnergy += filtered * filtered;

    if (++periodCounter >= (int) periodSamples)
    {
        lastPeriodRms = std::sqrt (periodEnergy / (float) periodCounter);
        periodEnergy  = 0.0f;
        periodCounter = 0;
        periodCompleted();
    }

    // Sub: a sine an octave below. Its period is exactly twice the string's
    // own (not twice the nominal f0, which the loop misses by a fraction of a
    // sample), or the two would drift apart and phase against each other. Its
    // level comes from a continuous mean square, not the once-a-period RMS,
    // whose measuring window slides and would modulate the sub.
    // The string is heard where the pluck goes in, not at the far end of the
    // delay line: read there, every note waited one whole period before it
    // sounded (31 ms at C1, 120 ms at C-1), which played as latency.
    glideDamper (false);
    // (The pluck plus the returning wave, before the feedback gain, which
    // only makes up the loop's own losses: the level is what it always was.)
    auto output = applyDamper (excitation + filtered);
    if (params.subLevel > 0.0f)
    {
        subMeanSquare += (filtered * filtered - subMeanSquare) * subLevelCoeff;
        output += std::sin (twoPi * subPhase) * std::sqrt (subMeanSquare) * subGain * params.subLevel;

        subPhase += subPhaseStep;
        if (subPhase >= 1.0f)
            subPhase -= 1.0f;
    }

    // DC blocker (first-order high-pass), then velocity.
    const auto dcFree = output - dcIn1 + dcBlockerCoeff * dcOut1;
    dcIn1  = output;
    dcOut1 = dcFree;

    // Last line of defence: a voice that has run away is silenced and freed
    // rather than allowed to reach the output. A string that is working never
    // comes near this.
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

    // The excitation reaches the read tap one period after it is written, so
    // the first completed period after the exciter stops may still be partly
    // empty. Only from the second one on is lastPeriodRms the real level.
    ++periodsSinceExcitation;
    if (periodsSinceExcitation < 2)
        return;

    if (stage == Stage::exciting)
    {
        // The pluck (or bow stroke) is over: from here on the string only loses energy.
        referenceLevel = lastPeriodRms;
        setStage (Stage::decaying);
    }
    else if (stage == Stage::decaying && params.sustainLevel > 0.0f
             && periodsSinceExcitation >= minimumPeriodsBeforeHold)
    {
        // Sustain: once the string has dropped to the hold level, stop damping
        // it. Never before it has settled, though, however high the sustain:
        // the loop stops damping when it holds, so whatever is still ringing
        // at that moment rings for ever.
        const auto holdDb    = -sustainRangeDb * (1.0f - params.sustainLevel);
        const auto holdLevel = referenceLevel * juce::Decibels::decibelsToGain (holdDb);

        if (lastPeriodRms <= holdLevel)
            setStage (Stage::holding);
    }

    if (lastPeriodRms < silenceThreshold)
        stringIsSilent = true;
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

//==============================================================================
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

float KarplusVoice::currentDamperPosition() const noexcept
{
    // The position LFO swings the finger either side of where it is set. It
    // never lifts it off: a damper that is off stays off.
    if (params.damperModulation == nullptr || mirroredPosition (params.damperPosition) <= 0.0f)
        return params.damperPosition;

    return juce::jlimit (pickPositionMinimum, positionMaximum, damperModulated);
}

float KarplusVoice::currentDamperPressure() const noexcept
{
    return juce::jlimit (0.0f, 1.0f, params.pressureModulation != nullptr ? pressureModulated : params.damperPressure);
}

float KarplusVoice::damperDelayFor (float position) const noexcept
{
    // A finger at p touches the same nodes as one at 1 - p, so the filter
    // works on the near side of the middle. At either end it touches nothing
    // that moves, which is off.
    position = mirroredPosition (position);
    if (position <= 0.0f)
        return 0.0f;

    // Fractional, so a moving finger slides the notches rather than stepping them.
    const auto maxDelay = ((float) damperHistory.size() - 4.0f) / (float) damperStages;
    return juce::jlimit (1.0f, maxDelay, position * periodSamples);
}

void KarplusVoice::updateDamperFilter() noexcept
{
    // Where the damper is heading. Off is a depth of zero at the last place
    // it was, so it fades out rather than vanishing.
    const auto delay = damperDelayFor (currentDamperPosition());
    damperMixTarget = delay > 0.0f ? maximumDamperMix * currentDamperPressure() : 0.0f;
    if (delay > 0.0f)
        damperDelayTarget = delay;

    // Coming on from nowhere: start at the place, and let the depth fade in.
    if (damperDelay <= 0.0f)
        damperDelay = damperDelayTarget;

    // The listening point is the pick, over the whole string.
    listenDelayTarget = juce::jlimit (1.0f, (float) listenHistory.size() * 0.2f,
                                      juce::jlimit (pickPositionMinimum, positionMaximum, params.pickPosition) * periodSamples);
}

void KarplusVoice::glideDamper (bool snap) noexcept
{
    if (snap)
        listenDelay = listenDelayTarget;

    if (juce::exactlyEqual (damperDelay, damperDelayTarget) && juce::exactlyEqual (damperMix, damperMixTarget))
        return;

    if (snap)
    {
        damperDelay = damperDelayTarget;
        damperMix   = damperMixTarget;
        listenDelay = listenDelayTarget;
    }
    else
    {
        damperDelay += (damperDelayTarget - damperDelay) * damperGlideCoeff;
        damperMix   += (damperMixTarget   - damperMix)   * damperGlideCoeff;
        if (std::abs (damperDelayTarget - damperDelay) < 1.0e-3f) damperDelay = damperDelayTarget;
        if (std::abs (damperMixTarget   - damperMix)   < 1.0e-5f) damperMix   = damperMixTarget;
    }

    // The binomial weights of ((1 - m) + m z^-D)^N, tap by tap.
    const auto a = 1.0f - damperMix, b = damperMix;
    std::array<float, 7> powA {}, powB {};
    powA[0] = powB[0] = 1.0f;
    for (int k = 1; k <= damperStages; ++k)
    {
        powA[(size_t) k] = powA[(size_t) k - 1] * a;
        powB[(size_t) k] = powB[(size_t) k - 1] * b;
    }

    float binomial = 1.0f;
    for (int k = 0; k <= damperStages; ++k)
    {
        damperWeights[(size_t) k] = binomial * powA[(size_t) (damperStages - k)] * powB[(size_t) k];
        binomial = binomial * (float) (damperStages - k) / (float) (k + 1);
    }
}

float KarplusVoice::readHistory (const std::vector<float>& history, int writeIndex, float delay) noexcept
{
    const auto size  = (int) history.size();
    const auto whole = (int) delay;
    const auto fraction = delay - (float) whole;

    auto index = writeIndex - 1 - whole;   // the newest sample sits just behind the write index
    while (index < 0)
        index += size;

    auto value = history[(size_t) index];
    if (fraction > 0.0f)
        value += fraction * (history[(size_t) (index > 0 ? index - 1 : size - 1)] - value);
    return value;
}

float KarplusVoice::readDamperHistory (float delay) const noexcept
{
    return readHistory (damperHistory, damperWriteIndex, delay);
}

float KarplusVoice::applyDamper (float input) noexcept
{
    // Where the string is heard from: the point it is plucked at, as a
    // pickup under the pick would hear it. A point q along the string picks
    // up mode n as sin (n pi q), which is the comb (1 - z^-(qP)) / 2. The
    // pluck position used to act only at the moment of the pluck, so moving
    // the pick while a note rang changed nothing until the next one; now it
    // reshapes the ringing string the same way it shapes the pluck, sliding
    // as it moves. After the string, not in it, so it cannot feed back.
    listenHistory[(size_t) listenWriteIndex] = input;
    if (++listenWriteIndex >= (int) listenHistory.size())
        listenWriteIndex = 0;

    listenDelay += (listenDelayTarget - listenDelay) * damperGlideCoeff;
    const auto heard = 0.5f * (input - readHistory (listenHistory, listenWriteIndex, listenDelay));

    // The damper works on what is heard. Its history always runs, so it has
    // the string's recent past to work on the moment it comes on.
    damperHistory[(size_t) damperWriteIndex] = heard;
    if (++damperWriteIndex >= (int) damperHistory.size())
        damperWriteIndex = 0;

    float shaped = heard;
    if (damperMix > 0.0f)
    {
        shaped = 0.0f;
        for (int k = 0; k <= damperStages; ++k)
            if (damperWeights[(size_t) k] != 0.0f)
                shaped += damperWeights[(size_t) k] * readDamperHistory ((float) k * damperDelay);
    }

    // Level make-up. The reference is the string itself, read at the two
    // filters' centre of gravity (half the pickup delay, plus N m D for the
    // damper), so the two rise together at the pluck and the gain does not
    // overshoot it. Both are mean squares over the same short window, so
    // their ratio holds still through the envelope: the pickup and the damper
    // change the colour, not the level, and not the shape of the note in time.
    const auto reference = readHistory (listenHistory, listenWriteIndex,
                                        0.5f * listenDelay + (damperMix > 0.0f ? (float) damperStages * damperMix * damperDelay : 0.0f));
    damperLevelIn  += (reference * reference - damperLevelIn)  * damperLevelCoeff;
    damperLevelOut += (shaped * shaped       - damperLevelOut) * damperLevelCoeff;

    if (++damperMakeupCounter >= damperMakeupInterval)
    {
        damperMakeupCounter = 0;
        if (damperLevelOut > 1.0e-14f)
            damperMakeupTarget = juce::jlimit (1.0f, damperMakeupLimit, std::sqrt (damperLevelIn / damperLevelOut));
    }

    damperMakeup += (damperMakeupTarget - damperMakeup) * damperLevelCoeff;
    return shaped * damperMakeup;
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

void KarplusVoice::setStage (Stage newStage) noexcept
{
    stage = newStage;

    // Holding raises the feedback to the cap, which is the loop's own loss at
    // f0 made up exactly, so the fundamental holds; renderNextBlock eases the
    // low-pass to its light holding setting. Taking the low-pass out
    // altogether (as this once did) froze whatever top end the string still
    // had into a loop that no longer lost anything: on low notes a corner in
    // the waveform once a period, a quiet tick at the note's own rate
    // (measured on E2 at full sustain: a spike every 12 ms). Doing it all at
    // once also moved the loop delay, and that retune clicked.
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
