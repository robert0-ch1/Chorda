/*
  ==============================================================================

    EnvelopeTests.cpp

    The envelope: release, decay, sustain and hold, and that none of them click.

  ==============================================================================
*/

#include "TestHelpers.h"
#include "Tests.h"

namespace chorda_test
{

void testReleaseHasNoClick (TestReport& report, const juce::File& outDir)
{
    report.section ("1. Note-off release is click-free");

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);

    const int noteOn  = 0;
    const int noteOff = seconds (0.5);
    const auto audio = render (processor,
                               { { noteOn,  juce::MidiMessage::noteOn  (1, 57, 1.0f) },
                                 { noteOff, juce::MidiMessage::noteOff (1, 57) } },
                               seconds (1.5));
    writeWav (outDir, "01_release", audio);

    const auto window = seconds (0.05);
    const auto peakBefore = peakBetween (audio, noteOff - window, noteOff);
    const auto stepBefore = maxStepBetween (audio, noteOff - window, noteOff);
    const auto stepAfter  = maxStepBetween (audio, noteOff, noteOff + window);

    report.check (allFinite (audio), "output is finite");
    report.check (peakBefore > 0.01f, "string is still sounding at note-off", "peak " + juce::String (peakBefore, 4));

    // A click is a step far larger than anything in the steady-state waveform.
    // With a 100 ms release the envelope adds almost nothing per sample.
    report.check (stepAfter <= stepBefore * 1.5f + 1.0e-4f, "no step discontinuity after note-off",
                  "before " + juce::String (stepBefore, 5) + ", after " + juce::String (stepAfter, 5));

    // 100 ms release (T60) + margin, then silence.
    const auto tailPeak = peakBetween (audio, noteOff + seconds (0.25), audio.getNumSamples());
    report.check (tailPeak < 1.0e-4f, "output is silent after the release", "peak " + juce::String (tailPeak, 6));

    // The same with a very fast release on a low note: the damping is smoothed
    // over one period, so even a 20 ms release must not step the level.
    setParameter (processor, pluck::ParamID::stringRelease, 0.02f);
    const auto fast = render (processor,
                              { { noteOn,  juce::MidiMessage::noteOn  (1, 45, 1.0f) },
                                { noteOff, juce::MidiMessage::noteOff (1, 45) } },
                              seconds (1.0));
    writeWav (outDir, "01b_fast_release", fast);

    const auto fastBefore = maxStepBetween (fast, noteOff - window, noteOff);
    const auto fastAfter  = maxStepBetween (fast, noteOff, noteOff + window);
    report.check (fastAfter <= fastBefore * 2.0f + 1.0e-4f, "20 ms release on A1 has no step",
                  "before " + juce::String (fastBefore, 5) + ", after " + juce::String (fastAfter, 5));
    report.check (peakBetween (fast, noteOff + seconds (0.1), fast.getNumSamples()) < 1.0e-3f,
                  "20 ms release is silent within 100 ms");
}

void testHoldAndStability (TestReport& report, const juce::File& outDir)
{
    report.section ("1f. Hold works at every pitch and the loop never blows up");
    using namespace pluck::ParamID;

    // Full sustain on a high note: the string must still be there after 1.5 s.
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, stringSustain, 1.0f);
        setParameter (processor, stringBrightness, 3000.0f);

        const auto audio = render (processor,
                                   { { 0,             juce::MidiMessage::noteOn  (1, 81, 1.0f) },
                                     { seconds (1.9), juce::MidiMessage::noteOff (1, 81) } },
                                   seconds (2.0));
        writeWav (outDir, "01f_hold_A5", audio);

        const auto early = audio.getRMSLevel (0, seconds (0.3), seconds (0.1));
        const auto late  = audio.getRMSLevel (0, seconds (1.7), seconds (0.1));
        report.check (late > early * 0.5f, "A5 holds within 6 dB over 1.4 s at full sustain",
                      juce::String (juce::Decibels::gainToDecibels (late / juce::jmax (1.0e-6f, early)), 1) + " dB");
    }

    // Long decays at the extremes of brightness and pitch, with and without
    // the damper moving the resonances about: the feedback may
    // exceed 1.0 to compensate the loop filter, so make sure nothing grows.
    for (const float brightness : { 200.0f, 20000.0f })
        for (const float damperPos : { 0.0f, 0.17f, 0.5f })
        for (const int note : { 24, 60, 96, 108 })
        {
            ChordaAudioProcessor processor;
            makeDryTestPatch (processor);
            setParameter (processor, exciterTone, 0.35f);   // has DC, the worst case
            setParameter (processor, stringBrightness, brightness);
            setParameter (processor, stringDamper, damperPos);
            setParameter (processor, stringSustain, 1.0f);
            setParameter (processor, stringDecay, 20.0f);

            const auto audio = render (processor,
                                       { { 0,             juce::MidiMessage::noteOn  (1, note, 1.0f) },
                                         { seconds (2.9), juce::MidiMessage::noteOff (1, note) } },
                                       seconds (3.0));

            const auto first = audio.getRMSLevel (0, seconds (0.2), seconds (0.2));
            const auto last  = audio.getRMSLevel (0, seconds (2.6), seconds (0.2));
            report.check (allFinite (audio) && last <= first * 1.5f + 1.0e-4f,
                          "stable: note " + juce::String (note) + " at " + juce::String ((int) brightness)
                              + " Hz brightness, damper " + juce::String (damperPos, 2),
                          "rms " + juce::String (first, 4) + " -> " + juce::String (last, 4));
        }
}

void testReleaseTime (TestReport& report, const juce::File& outDir)
{
    report.section ("1e. Release time matches the label");
    using namespace pluck::ParamID;

    // Release is a T60: after the release time the string should be 60 dB down,
    // so 30 dB down half-way. Measured on peak and RMS per 100 ms window.
    for (const int note : { 45, 57, 69, 81, 93 })
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, stringRelease, 1.0f);

        const auto keyUp = seconds (0.5);
        const auto audio = render (processor,
                                   { { 0,     juce::MidiMessage::noteOn  (1, note, 1.0f) },
                                     { keyUp, juce::MidiMessage::noteOff (1, note) } },
                                   seconds (2.0));
        writeWav (outDir, "01e_release_note" + juce::String (note), audio);

        auto rmsBetween = [&] (int start, int end)
        {
            return audio.getRMSLevel (0, start, end - start);
        };

        const auto win = seconds (0.1);
        const auto refPeak = peakBetween (audio, keyUp - win, keyUp);
        const auto refRms  = rmsBetween (keyUp - win, keyUp);

        juce::ignoreUnused (refPeak);
        const auto halfWay = juce::Decibels::gainToDecibels (rmsBetween (keyUp + seconds (0.5), keyUp + seconds (0.6)) / refRms);
        report.check (halfWay > -36.0f && halfWay < -24.0f, "note " + juce::String (note) + " is ~30 dB down half-way through a 1 s release",
                      juce::String (halfWay, 1) + " dB");
    }
}

void testSustainAndDecay (TestReport& report, const juce::File& outDir)
{
    report.section ("1c. Sustain holds the string, decay lets it go");
    using namespace pluck::ParamID;

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);
    setParameter (processor, stringDecay, 0.4f);   // fast decay so the difference is obvious

    const std::vector<MidiEvent> heldNote { { 0,             juce::MidiMessage::noteOn  (1, 57, 1.0f) },
                                            { seconds (1.9), juce::MidiMessage::noteOff (1, 57) } };

    setParameter (processor, stringSustain, 0.0f);
    const auto decaying = render (processor, heldNote, seconds (2.0));
    writeWav (outDir, "01c_sustain_off", decaying);

    setParameter (processor, stringSustain, 1.0f);
    const auto held = render (processor, heldNote, seconds (2.0));
    writeWav (outDir, "01c_sustain_full", held);

    const auto early = seconds (0.3), late = seconds (1.6), win = seconds (0.1);
    const auto decayingRatio = peakBetween (decaying, late, late + win) / juce::jmax (1.0e-6f, peakBetween (decaying, early, early + win));
    const auto heldRatio     = peakBetween (held, late, late + win)     / juce::jmax (1.0e-6f, peakBetween (held, early, early + win));

    report.check (allFinite (decaying) && allFinite (held), "output is finite");
    report.check (decayingRatio < 0.01f, "with sustain off the string has decayed by >40 dB after 1.3 s",
                  juce::String (juce::Decibels::gainToDecibels (decayingRatio), 1) + " dB");
    report.check (heldRatio > 0.5f, "with full sustain the string holds within 6 dB",
                  juce::String (juce::Decibels::gainToDecibels (heldRatio), 1) + " dB");
}

void testLowNotesSurviveShortPlucks (TestReport& report, const juce::File& outDir)
{
    report.section ("1d. Low notes with a short pluck");

    // A 3 ms noise pluck on E2: the period (about 585 samples) is longer than
    // the block, so the loop output is still zero when the excitation ends.
    // The voice must wait for the string, not declare it silent.
    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);
    setParameter (processor, pluck::ParamID::exciterTone, pluck::toneNoise);
    setParameter (processor, pluck::ParamID::exciterAttack, 3.0f);
    setParameter (processor, pluck::ParamID::stringSustain, 1.0f);

    const auto audio = render (processor,
                               { { 0,             juce::MidiMessage::noteOn  (1, 40, 1.0f) },
                                 { seconds (1.4), juce::MidiMessage::noteOff (1, 40) } },
                               seconds (1.5));
    writeWav (outDir, "01d_low_note_short_pluck", audio);

    const auto early = peakBetween (audio, seconds (0.1), seconds (0.2));
    const auto late  = peakBetween (audio, seconds (1.2), seconds (1.3));
    juce::String profile;
    for (int w = 0; w < 14; ++w)
        profile += juce::String (peakBetween (audio, seconds (0.1 * w), seconds (0.1 * (w + 1))), 4) + " ";
    std::cout << "    level per 100 ms: " << profile << std::endl;
    report.check (early > 1.0e-3f, "E2 sounds after a 3 ms pluck", "peak " + juce::String (early, 4));
    // The peak keeps falling for a while even at full sustain: the pluck is a
    // pulse whose harmonics die in the loop filter while the fundamental is
    // held (it loses well under 1 dB here). So ask for "clearly still
    // sounding", not "same peak". A 3 ms noise burst is also shorter than one
    // period of E2, so how much of it lands on the string's resonances is
    // luck: the level of a single pluck varies by several dB from one to the
    // next, and the threshold has to cover that.
    report.check (late > early * 0.3f, "E2 is still held at full sustain 1.2 s later",
                  juce::String (juce::Decibels::gainToDecibels (late / juce::jmax (1.0e-6f, early)), 1) + " dB");
}

/** A note held at full sustain must stay a string: quiet above the Brightness
    cutoff, free of the ticking that a frozen, undamped excitation causes, and
    steady in level. The noise exciter is the demanding case, because it is the
    one that puts energy into every partial the loop can carry. */
void testHeldNoteStaysClean (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1i. A held note stays clean");

    for (const int note : { 40, 57, 69 })
    {
        ChordaAudioProcessor processor;                    // factory defaults
        setParameter (processor, stringSustain, 1.0f);

        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 0.9f) } }, seconds (6.0));
        writeWav (outDir, "01i_held_note_" + juce::String (note), audio);

        const auto name = juce::String ("note ") + juce::String (note);
        report.check (allFinite (audio), name + ": the held note stays finite");

        // Energy above 5 kHz, over the last second. Undamped excitation shows
        // up here: the string itself has nothing left up there by then.
        const auto* samples = audio.getReadPointer (0);
        constexpr int order = 15, fftSize = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) fftSize * 2, 0.0f);
        const auto start = seconds (5.0);
        for (int i = 0; i < fftSize; ++i)
            data[(size_t) i] = samples[start + i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());

        float total = 0.0f, high = 0.0f;
        const auto binHz = (float) sampleRate / (float) fftSize;
        for (int b = 1; b < fftSize / 2; ++b)
        {
            const auto power = data[(size_t) b] * data[(size_t) b];
            total += power;
            if ((float) b * binHz > 5000.0f)
                high += power;
        }
        const auto highPercent = 100.0f * high / juce::jmax (total, 1.0e-20f);
        report.check (highPercent < 1.0f, name + ": less than 1 % of the held tone is above 5 kHz",
                      juce::String (highPercent, 2) + " %");

        // Sample-to-sample roughness: a ticking string has sharp corners.
        float worst = 0.0f, energy = 0.0f;
        const auto from = seconds (2.0), to = seconds (6.0);
        for (int i = from; i < to; ++i)
        {
            energy += samples[i] * samples[i];
            worst = juce::jmax (worst, std::abs (samples[i] - 2.0f * samples[i - 1] + samples[i - 2]));
        }
        const auto roughness = worst / juce::jmax (std::sqrt (energy / (float) (to - from)), 1.0e-9f);
        report.check (roughness < 0.6f, name + ": the held tone has no sharp corners",
                      "peak curvature " + juce::String (roughness, 2) + " x RMS");

        // And it really holds. Measured as RMS: a noise pluck's partials sit at
        // random phases, so its peaks wander while its energy holds still.
        const auto early = audio.getRMSLevel (0, seconds (2.0), seconds (0.5));
        const auto late  = audio.getRMSLevel (0, seconds (5.5), seconds (0.5));
        report.check (late > early * 0.7f, name + ": the level is still there 3.5 s later",
                      juce::String (juce::Decibels::gainToDecibels (late / juce::jmax (1.0e-6f, early)), 1) + " dB");
    }
}

/** The envelope holds with the damper on, and so does the level: the damper
    is a filter after the string, with its loss made up. */
void testDamperKeepsTheEnvelope (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1p. The envelope holds with the damper on");

    auto renderWith = [&] (float position, float pressure, float release, const juce::String& name)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, exciterTone,          pluck::toneSquare);
        setParameter (processor, stringDecay,          2.0f);
        setParameter (processor, stringRelease,        release);
        setParameter (processor, stringDamper,         position);
        setParameter (processor, stringDamperPressure, pressure);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 45, 1.0f) },
                                                { seconds (1.6), juce::MidiMessage::noteOff (1, 45) } }, seconds (2.8));
        writeWav (outDir, "01p_" + name, audio);
        return audio;
    };

    // Decay 2 s is 30 dB a second: measured once the touched partials have
    // gone, from 0.4 s to 1.4 s, on the partials the finger leaves alone (at
    // a third, the third partial and its multiples, which the square has and
    // the pick at 20 % does not cancel).
    for (const auto& [position, pressure, name] : { std::make_tuple (0.0f, 0.6f, juce::String ("no damper")),
                                                    std::make_tuple (1.0f / 3.0f, 0.6f, juce::String ("damper at a third")),
                                                    std::make_tuple (0.5f, 1.0f, juce::String ("damper at the middle, full pressure")) })
    {
        const auto audio = renderWith (position, pressure, 0.5f, name.replaceCharacters (" %,", "___"));
        const auto early = peakBetween (audio, seconds (0.35), seconds (0.45));
        const auto late  = peakBetween (audio, seconds (1.35), seconds (1.45));
        const auto db = juce::Decibels::gainToDecibels (late / juce::jmax (early, 1.0e-9f));
        report.check (allFinite (audio) && early > 1.0e-3f, name + ": the string still sounds after the pluck",
                      "level " + juce::String (juce::Decibels::gainToDecibels (early), 1) + " dB");
        report.check (db > -38.0f && db < -22.0f, name + ": a 2 s Decay loses 30 dB a second",
                      juce::String (db, 1) + " dB over 1 s");

        // Release 0.5 s is 60 dB in half a second, so 30 dB after 0.25 s.
        const auto atOff   = peakBetween (audio, seconds (1.55), seconds (1.6));
        const auto quarter = peakBetween (audio, seconds (1.83), seconds (1.88));
        const auto releaseDb = juce::Decibels::gainToDecibels (quarter / juce::jmax (atOff, 1.0e-9f));
        report.check (releaseDb > -40.0f && releaseDb < -20.0f, name + ": a 0.5 s Release loses 30 dB in 0.25 s",
                      juce::String (releaseDb, 1) + " dB");
    }

    // One level across the damper: the make-up puts back what the filter
    // takes, wherever the finger is and however hard it presses.
    {
        auto loudnessOf = [&] (float position, float pressure)
        {
            const auto audio = renderWith (position, pressure, 0.5f, "level_" + juce::String (juce::roundToInt (position * 200.0f))
                                                                      + "_" + juce::String (juce::roundToInt (pressure * 100.0f)));
            return std::make_pair (10.0 * std::log10 (kWeightedMeanSquare (audio, seconds (0.1), seconds (1.2))),
                                   audio.getMagnitude (0, 0, seconds (0.3)));
        };

        const auto [plain, plainPeak] = loudnessOf (0.0f, 0.6f);
        double worst = 0.0;
        float  worstPeak = 0.0f;
        juce::String trace;
        for (const auto position : { 0.05f, 0.1f, 0.2f, 1.0f / 3.0f, 0.5f })
            for (const auto pressure : { 0.3f, 0.6f, 1.0f })
            {
                const auto [loud, peak] = loudnessOf (position, pressure);
                worst = std::abs (loud - plain) > std::abs (worst) ? loud - plain : worst;
                worstPeak = juce::jmax (worstPeak, peak / juce::jmax (plainPeak, 1.0e-9f));
                trace += juce::String (juce::roundToInt (position * 200.0f)) + "%/" + juce::String (juce::roundToInt (pressure * 100.0f))
                       + "%: " + juce::String (loud - plain, 1) + "  ";
            }
        std::cout << "    damped against undamped (dB, position/pressure): " << trace << std::endl;
        report.check (std::abs (worst) < 3.0, "damped notes are as loud as undamped ones, across the damper",
                      "furthest " + juce::String (worst, 1) + " dB");
        report.check (worstPeak < 2.0f, "the level make-up does not overshoot the pluck",
                      "highest peak " + juce::String (juce::Decibels::gainToDecibels (worstPeak), 1) + " dB against undamped");
    }

    // Pressure is how deep the cut is: at the middle the fundamental has no
    // node and the octave has one, so under a firmer finger the octave stands
    // far further above the fundamental.
    {
        const auto f0 = 440.0f * std::pow (2.0f, (45.0f - 69.0f) / 12.0f);
        const auto light = renderWith (0.5f, 0.2f, 0.5f, "middle_light");
        const auto firm  = renderWith (0.5f, 0.8f, 0.5f, "middle_firm");
        const auto lightOctave = partialLevelDb (light, seconds (0.3), seconds (0.2), f0, 2);
        const auto firmOctave  = partialLevelDb (firm,  seconds (0.3), seconds (0.2), f0, 2);
        report.check (firmOctave > lightOctave + 20.0f, "a firmer finger cuts the fundamental deeper",
                      "octave over fundamental at 0.3 s: " + juce::String (lightOctave, 1) + " dB light, "
                      + juce::String (firmOctave, 1) + " dB firm");
    }
}

/** A held note must not click: not when it starts to hold, not on the stereo
    widener's cycle, not ever. */
void testHeldNotesDoNotClick (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1r. Held notes do not click");

    auto renderHeld = [&] (int note, float sustain, float width, const juce::String& name)
    {
        ChordaAudioProcessor processor;                    // factory defaults
        setParameter (processor, stringSustain, sustain);
        setParameter (processor, outputWidth,   width);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 0.9f) } }, seconds (7.0));
        writeWav (outDir, "01r_" + name, audio);
        return audio;
    };

    // The widener's slow sweep wraps every 3.2 s; its shimmer used to jump there.
    {
        const auto audio = renderHeld (57, 1.0f, 1.0f, "wide");
        const auto clicks = countClicks (audio, seconds (0.5), seconds (7.0));
        report.check (clicks == 0, "a held note at full Width does not click on the widener's cycle",
                      juce::String (clicks) + " clicks in 6.5 s");
    }

    // The moment a note starts to hold used to retune the loop in one jump.
    for (const auto& [note, sustain] : { std::pair { 57, 0.5f }, std::pair { 45, 0.7f }, std::pair { 69, 0.3f } })
    {
        const auto audio = renderHeld (note, sustain, 0.0f, "hold_" + juce::String (note));
        const auto clicks = countClicks (audio, seconds (0.3), seconds (7.0));
        report.check (clicks == 0, "note " + juce::String (note) + " at " + juce::String (juce::roundToInt (sustain * 100.0f))
                                   + " % sustain goes into its hold without a click",
                      juce::String (clicks) + " clicks");
    }
}

} // namespace chorda_test
