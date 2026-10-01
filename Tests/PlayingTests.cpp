/*
  ==============================================================================

    PlayingTests.cpp

    Playing: voice modes, polyphony, stealing, glide, legato and the LFOs.

  ==============================================================================
*/

#include "TestHelpers.h"
#include "Tests.h"

namespace chorda_test
{

/** Velocity and mod wheel brighten, octave transposes, full damper pressure stays bounded and in tune. */
void testPerformanceControls (TestReport& report, const juce::File& outDir)
{
    report.section ("1j. Velocity, mod wheel, octave, damper pressure");
    using namespace pluck::ParamID;

    auto renderNote = [&] (std::function<void (ChordaAudioProcessor&)> setup, int note, float velocity,
                           std::vector<MidiEvent> extra = {})
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, exciterTone, 0.35f);   // rich in harmonics, and deterministic
        setParameter (processor, exciterAttack, 2.0f);
        setParameter (processor, stringSustain, 0.0f);   // keep the loop low-pass in: "hold" bypasses it
        setup (processor);
        std::vector<MidiEvent> events { { 0, juce::MidiMessage::noteOn (1, note, velocity) },
                                        { seconds (1.4), juce::MidiMessage::noteOff (1, note) } };
        events.insert (events.end(), extra.begin(), extra.end());
        return render (processor, events, seconds (1.5));
    };

    // Velocity: a hard pluck is brighter (more energy in the upper partials).
    {
        const auto soft = renderNote ([] (auto&) {}, 57, 0.2f);
        const auto hard = renderNote ([] (auto&) {}, 57, 1.0f);
        // 4th partial: the test patch picks at 20 %, a node of the 5th.
        const auto softHigh = partialLevelDb (soft, seconds (0.1), seconds (0.3), 220.0f, 4);
        const auto hardHigh = partialLevelDb (hard, seconds (0.1), seconds (0.3), 220.0f, 4);
        report.check (hardHigh > softHigh + 3.0f, "hard velocity is brighter than soft",
                      "4th/1st " + juce::String (softHigh, 1) + " dB -> " + juce::String (hardHigh, 1) + " dB");
    }

    // Mod wheel: CC1 at full raises brightness too.
    {
        const auto plain = renderNote ([] (auto&) {}, 57, 0.6f);
        const auto wheel = renderNote ([] (auto&) {}, 57, 0.6f, { { 0, juce::MidiMessage::controllerEvent (1, 1, 127) } });
        const auto plainHigh = partialLevelDb (plain, seconds (0.5), seconds (0.5), 220.0f, 4);
        const auto wheelHigh = partialLevelDb (wheel, seconds (0.5), seconds (0.5), 220.0f, 4);
        report.check (wheelHigh > plainHigh + 3.0f, "mod wheel raises brightness",
                      "4th/1st " + juce::String (plainHigh, 1) + " dB -> " + juce::String (wheelHigh, 1) + " dB");
    }

    // Octave: +12 plays 440 for A3.
    {
        const auto up = renderNote ([] (auto& p) { setParameter (p, pitchOctave, 3.0f); }, 57, 0.8f);
        const auto measured = estimateFrequency (up, seconds (0.4), seconds (0.6), 440.0f);
        report.check (std::abs (1200.0f * std::log2 (measured / 440.0f)) < 2.0f, "+12 transposes A3 to A4",
                      juce::String (measured, 2) + " Hz");
    }

    // Damper pressure at maximum: stable, and the surviving octave partial stays in tune.
    {
        const auto pressed = renderNote ([] (auto& p) { setParameter (p, stringDamper, 0.5f); setParameter (p, stringDamperPressure, 1.0f); }, 57, 1.0f);
        writeWav (outDir, "01j_damper_full_pressure", pressed);
        const auto peak = pressed.getMagnitude (0, pressed.getNumSamples());
        const auto octave = estimateFrequency (pressed, seconds (0.5), seconds (0.5), 440.0f);
        report.check (allFinite (pressed) && peak < 1.0f, "full damper pressure stays bounded", "peak " + juce::String (peak, 3));
        report.check (std::abs (1200.0f * std::log2 (octave / 440.0f)) < 3.0f, "octave harmonic stays in tune under full pressure",
                      juce::String (octave, 2) + " Hz");
    }
}

/** Polyphony limits cap the active voices; Mono uses one. */
void testVoiceModes (TestReport& report)
{
    report.section ("1i. Voice modes");
    using namespace pluck::ParamID;

    // Polyphony limit of 4: six simultaneous notes leave four voices sounding.
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode, (float) pluck::voiceModeNames.indexOf ("4"));
        std::vector<MidiEvent> events;
        for (int i = 0; i < 6; ++i)
            events.push_back ({ 0, juce::MidiMessage::noteOn (1, 48 + 2 * i, 0.9f) });
        render (processor, events, seconds (0.1));
        report.check (processor.getActiveVoiceCount() == 4, "polyphony 4 caps six notes at four voices",
                      juce::String (processor.getActiveVoiceCount()) + " active");
    }

    // Mono: the second note steals the only voice and re-plucks.
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode, (float) pluck::voiceModeMono);
        render (processor, { { 0, juce::MidiMessage::noteOn (1, 57, 0.9f) },
                             { seconds (0.05), juce::MidiMessage::noteOn (1, 64, 0.9f) } }, seconds (0.1));
        report.check (processor.getActiveVoiceCount() == 1, "mono keeps a single voice",
                      juce::String (processor.getActiveVoiceCount()) + " active");
    }

}

/** Releasing one note leaves the other voices sounding. */
void testPolyphonyIsIndependent (TestReport& report, const juce::File& outDir)
{
    report.section ("2. Releasing one note keeps the others sounding");

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);

    // Reference: E alone, measured at 0.7 to 0.9 s.
    const auto solo = render (processor,
                              { { seconds (0.1), juce::MidiMessage::noteOn  (1, 64, 0.9f) },
                                { seconds (1.0), juce::MidiMessage::noteOff (1, 64) } },
                              seconds (1.5));

    // Same with A underneath, released at 0.5 s: E must keep at least half its level.
    const auto audio = render (processor,
                               { { 0,             juce::MidiMessage::noteOn  (1, 57, 0.9f) },
                                 { seconds (0.1), juce::MidiMessage::noteOn  (1, 64, 0.9f) },
                                 { seconds (0.5), juce::MidiMessage::noteOff (1, 57) },
                                 { seconds (1.0), juce::MidiMessage::noteOff (1, 64) } },
                               seconds (1.5));
    writeWav (outDir, "02_polyphony", audio);

    const auto soloPeak      = peakBetween (solo,  seconds (0.7), seconds (0.9));
    const auto withOtherPeak = peakBetween (audio, seconds (0.7), seconds (0.9));
    const auto peakAfterAll  = peakBetween (audio, seconds (1.3), seconds (1.5));

    report.check (allFinite (audio), "output is finite");
    report.check (soloPeak > 1.0e-3f, "reference note is audible", "peak " + juce::String (soloPeak, 4));
    report.check (withOtherPeak > soloPeak * 0.5f, "second note survives the first note's release",
                  "solo " + juce::String (soloPeak, 4) + ", with other note " + juce::String (withOtherPeak, 4));
    report.check (peakAfterAll < 1.0e-4f, "silence after both notes released", "peak " + juce::String (peakAfterAll, 6));
}

/** 40 notes on 16 voices: finite, bounded, fades out; a chord gets one voice per note. */
void testVoiceStealing (TestReport& report, const juce::File& outDir)
{
    report.section ("3. More notes than voices");

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);

    std::vector<MidiEvent> events;
    for (int i = 0; i < 40; ++i)                                             // 40 notes, 16 voices
        events.push_back ({ seconds (0.02 * i), juce::MidiMessage::noteOn (1, 40 + i, 1.0f) });
    for (int i = 0; i < 40; ++i)
        events.push_back ({ seconds (1.0 + 0.01 * i), juce::MidiMessage::noteOff (1, 40 + i) });

    const auto audio = render (processor, events, seconds (2.0));
    writeWav (outDir, "03_voice_stealing", audio);

    // Simultaneous notes must each get their own voice.
    render (processor, { { 0, juce::MidiMessage::noteOn (1, 52, 0.9f) },
                         { 0, juce::MidiMessage::noteOn (1, 59, 0.9f) },
                         { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) } }, seconds (0.05));
    report.check (processor.getActiveVoiceCount() == 3, "a three-note chord uses three voices",
                  juce::String (processor.getActiveVoiceCount()) + " active");

    const auto peak = audio.getMagnitude (0, audio.getNumSamples());
    report.check (allFinite (audio), "output is finite");
    report.check (peak > 0.01f, "audio was produced", "peak " + juce::String (peak, 3));
    report.check (peak < (float) pluck::numVoices, "level is bounded", "peak " + juce::String (peak, 3));
    report.check (peakBetween (audio, seconds (1.8), seconds (2.0)) < 1.0e-3f, "everything fades out");
}

/** Glide slides the string from the note before this one; Legato leaves it
    ringing and only changes where it is tuned. */
void testGlideAndLegato (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1m. Glide and legato");

    const auto lowHz  = (float) juce::MidiMessage::getMidiNoteInHertz (48);
    const auto highHz = (float) juce::MidiMessage::getMidiNoteInHertz (60);

    // Glide: a mono voice re-plucked on a new note starts at the old pitch.
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode,  (float) pluck::voiceModeNames.indexOf ("Mono"));
        setParameter (processor, pitchGlide, 1.5f);
        setParameter (processor, stringDecay, 8.0f);

        const auto audio = render (processor,
                                   { { 0,             juce::MidiMessage::noteOn  (1, 48, 1.0f) },
                                     { seconds (0.5), juce::MidiMessage::noteOff (1, 48) },
                                     { seconds (0.5), juce::MidiMessage::noteOn  (1, 60, 1.0f) } },
                                   seconds (3.0));
        writeWav (outDir, "01m_glide", audio);

        // Short window early in a long glide, since the pitch moves during the measurement.
        const auto atStart = estimateFrequency (audio, seconds (0.53), seconds (0.09), lowHz);
        const auto atEnd   = estimateFrequency (audio, seconds (2.4),  seconds (0.4),  highHz);

        report.check (std::abs (1200.0f * std::log2 (atStart / lowHz)) < 250.0f,
                      "the glide starts near the note before it",
                      juce::String (atStart, 1) + " Hz, from " + juce::String (lowHz, 1) + " Hz");
        report.check (std::abs (1200.0f * std::log2 (atEnd / highHz)) < 15.0f,
                      "and arrives in tune on the new note",
                      juce::String (atEnd, 1) + " Hz, to " + juce::String (highHz, 1) + " Hz");
        report.check (allFinite (audio), "the glide stays finite");
    }

    // Legato: a second key while the first is held must not re-pluck.
    auto secondNoteAttack = [&] (const char* mode)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode,  (float) pluck::voiceModeNames.indexOf (mode));
        setParameter (processor, pitchGlide, 0.001f);
        // Short enough that a re-pluck stands out, long enough that the string still rings for legato to slide.
        setParameter (processor, stringDecay, 1.5f);

        const auto audio = render (processor,
                                   { { 0,             juce::MidiMessage::noteOn (1, 48, 1.0f) },
                                     { seconds (0.8), juce::MidiMessage::noteOn (1, 60, 1.0f) } },
                                   seconds (1.6));
        writeWav (outDir, juce::String ("01m_") + mode, audio);

        const auto before = peakBetween (audio, seconds (0.70), seconds (0.79));
        const auto after  = peakBetween (audio, seconds (0.80), seconds (0.90));
        const auto pitch  = estimateFrequency (audio, seconds (0.85), seconds (0.3), highHz);
        return std::make_pair (juce::Decibels::gainToDecibels (after / juce::jmax (before, 1.0e-6f)), pitch);
    };

    const auto [monoJump, monoPitch]     = secondNoteAttack ("Mono");
    const auto [legatoJump, legatoPitch] = secondNoteAttack ("Legato");

    report.check (monoJump > 8.0f, "in Mono the second key plucks the string again",
                  juce::String (monoJump, 1) + " dB step");
    report.check (legatoJump < monoJump - 8.0f, "in Legato it does not",
                  juce::String (legatoJump, 1) + " dB step against Mono's " + juce::String (monoJump, 1) + " dB");
    report.check (std::abs (1200.0f * std::log2 (legatoPitch / highHz)) < 15.0f,
                  "but the string is retuned to the new note",
                  juce::String (legatoPitch, 1) + " Hz, wanted " + juce::String (highHz, 1) + " Hz");
}

/** A glide plucks at its start note, as loud as a plain pluck of that note. */
void testGlideLevel (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("1u. A glide is as loud as a plain pluck");

    auto secondPeak = [] (int from, int to, float glide)
    {
        ChordaAudioProcessor processor;
        setParameter (processor, exciterTone, pluck::toneSine);   // the same every time
        setParameter (processor, voiceMode,   (float) pluck::voiceModeMono);
        setParameter (processor, pitchGlide,  glide);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, from, 0.9f) },
                                                { seconds (0.8), juce::MidiMessage::noteOff (1, from) },
                                                { seconds (0.8), juce::MidiMessage::noteOn (1, to, 0.9f) } }, seconds (1.6));
        return peakBetween (audio, seconds (0.8), seconds (1.6));
    };

    for (const auto& [from, to] : { std::pair { 64, 40 }, std::pair { 40, 64 }, std::pair { 57, 45 } })
    {
        const auto plain  = secondPeak (from, from, 0.001f);
        const auto glided = secondPeak (from, to, 0.3f);
        const auto db = juce::Decibels::gainToDecibels (glided / juce::jmax (plain, 1.0e-9f));
        // 1.5 dB tolerance: a fast upward glide has a ~1 dB bump in its first 40 ms
        // from the pickup delay tracking the pitch.
        report.check (std::abs (db) < 1.5f, "a glide from " + juce::String (from) + " to " + juce::String (to) + " plucks as loud as " + juce::String (from),
                      juce::String (db, 2) + " dB");
    }
}

/** Renders with the processor's LFO sampled once per block, so a test can
    see the modulation itself rather than guess it from the audio. */
juce::AudioBuffer<float> renderWatchingLfo (ChordaAudioProcessor& processor, std::vector<MidiEvent> events,
                                            int totalSamples, std::vector<float>& lfo)
{
    std::sort (events.begin(), events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> output (2, totalSamples);
    juce::AudioBuffer<float> block (2, blockSize);
    size_t nextEvent = 0;
    lfo.clear();

    for (int position = 0; position < totalSamples; position += blockSize)
    {
        const auto numSamples = juce::jmin (blockSize, totalSamples - position);
        juce::MidiBuffer midi;
        while (nextEvent < events.size() && events[nextEvent].sample < position + numSamples)
        {
            midi.addEvent (events[nextEvent].message, juce::jmax (0, events[nextEvent].sample - position));
            ++nextEvent;
        }

        block.setSize (2, numSamples, false, false, true);
        block.clear();
        processor.processBlock (block, midi);
        lfo.push_back (processor.getDamperModulation());

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, position, block, ch, 0, numSamples);
    }

    processor.releaseResources();
    return output;
}

/** The LFO moves the damper along the string: Amount, Rate, Shape and Sync. */
void testLfo (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1n. The LFO moves the damper");

    auto patch = [] (ChordaAudioProcessor& p)
    {
        makeDryTestPatch (p);
        setParameter (p, stringDamper,         0.2f);
        setParameter (p, stringDamperPressure, 0.8f);
        setParameter (p, stringDecay,          6.0f);
    };

    const std::vector<MidiEvent> note { { 0, juce::MidiMessage::noteOn (1, 45, 1.0f) } };

    // At zero Amount the LFO is not there at all, whatever else it is set to.
    {
        ChordaAudioProcessor still, set;
        patch (still);
        patch (set);
        setParameter (set, lfoRate,  7.0f);
        setParameter (set, lfoSync,  1.0f);
        const auto a = render (still, note, seconds (1.0));
        const auto b = render (set,   note, seconds (1.0));

        float difference = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
            difference = juce::jmax (difference, std::abs (a.getSample (0, i) - b.getSample (0, i)));
        report.check (difference == 0.0f, "at zero Amount the LFO changes nothing, bit for bit",
                      "largest difference " + juce::String (difference, 8));
    }

    // 2 Hz for 3 s is twelve crossings; 20 % Amount swings the damper +/-10 % of the
    // string around its 20 % setting (100 % spans the whole string).
    {
        ChordaAudioProcessor processor;
        patch (processor);
        setParameter (processor, lfoAmount, 0.2f);
        setParameter (processor, lfoRate,   2.0f);
        std::vector<float> lfo;
        const auto audio = renderWatchingLfo (processor, note, seconds (3.0), lfo);
        writeWav (outDir, "01n_lfo_sine_2hz", audio);

        auto lowest = 1.0f, highest = 0.0f;
        for (size_t i = 4; i < lfo.size(); ++i) { lowest = juce::jmin (lowest, lfo[i]); highest = juce::jmax (highest, lfo[i]); }
        const auto count = crossings (lfo);
        report.check (allFinite (audio), "a moving damper stays finite");
        report.check (count >= 11 && count <= 13, "2 Hz is two cycles a second", juce::String (count) + " crossings in 3 s");
        report.check (std::abs (lowest - 0.1f) < 0.005f && std::abs (highest - 0.3f) < 0.005f,
                      "20 % Amount swings the damper from 10 % to 30 % around 20 %",
                      juce::String (juce::roundToInt (lowest * 100.0f)) + " % to " + juce::String (juce::roundToInt (highest * 100.0f)) + " %");

    }

    // A damper that is off stays off: the position LFO does not lift it on.
    {
        auto renderDamperOff = [&] (float amount)
        {
            ChordaAudioProcessor processor;
            patch (processor);
            setParameter (processor, stringDamper, 0.0f);
            setParameter (processor, lfoAmount,    amount);
            return render (processor, note, seconds (1.0));
        };
        const auto off = renderDamperOff (0.0f), moving = renderDamperOff (0.6f);
        float difference = 0.0f;
        for (int i = 0; i < off.getNumSamples(); ++i)
            difference = juce::jmax (difference, std::abs (off.getSample (0, i) - moving.getSample (0, i)));
        report.check (difference == 0.0f, "with the damper off, the position LFO leaves the string alone",
                      "largest difference " + juce::String (difference, 8));
    }

    // Pressure LFO: bipolar around the set 80 %, 20 % Amount is +/-10 %, at its own rate.
    {
        ChordaAudioProcessor processor;
        patch (processor);
        setParameter (processor, lfoPressureAmount, 0.2f);
        setParameter (processor, lfoPressureRate,   3.0f);

        processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        std::vector<float> pressure;
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
        bool finite = true;
        for (int position = 0; position < seconds (2.0); position += blockSize)
        {
            block.clear();
            processor.processBlock (block, midi);
            midi.clear();
            finite = finite && allFinite (block);
            pressure.push_back (processor.getPressureModulation());
        }
        processor.releaseResources();

        auto lowest = 1.0f, highest = 0.0f;
        for (size_t i = 4; i < pressure.size(); ++i) { lowest = juce::jmin (lowest, pressure[i]); highest = juce::jmax (highest, pressure[i]); }
        const auto count = crossings (pressure);
        report.check (finite, "a pressing and easing damper stays finite");
        report.check (count >= 11 && count <= 13, "the pressure LFO at 3 Hz runs at 3 Hz", juce::String (count) + " crossings in 2 s");
        report.check (std::abs (lowest - 0.7f) < 0.005f && std::abs (highest - 0.9f) < 0.005f,
                      "20 % Amount swings the pressure from 70 % to 90 % around 80 %",
                      juce::String (juce::roundToInt (lowest * 100.0f)) + " % to " + juce::String (juce::roundToInt (highest * 100.0f)) + " %");
        report.check (processor.getLfoRateText (true).endsWith ("Hz"), "the pressure rate reads on its own knob", processor.getLfoRateText (true));
    }

    // A held string under a fast, deep LFO and the hardest damper must not run away.
    {
        ChordaAudioProcessor processor;
        patch (processor);
        setParameter (processor, stringSustain,        1.0f);
        setParameter (processor, stringDamper,         0.5f);
        setParameter (processor, stringDamperPressure, 1.0f);
        setParameter (processor, lfoAmount,            1.0f);
        setParameter (processor, lfoRate,              20.0f);
        const auto audio = render (processor, note, seconds (8.0));
        writeWav (outDir, "01n_lfo_held", audio);
        const auto early = peakBetween (audio, seconds (0.5), seconds (1.5));
        const auto late  = peakBetween (audio, seconds (6.5), seconds (7.5));
        report.check (allFinite (audio) && late < early * 3.0f + 1.0e-4f, "a held string under a fast deep LFO stays bounded",
                      juce::String (juce::Decibels::gainToDecibels (late / juce::jmax (early, 1.0e-6f)), 1) + " dB over 6 s");
    }

    // Sync: with no host the tempo is 120 BPM, where 1.7 Hz snaps to a quarter note (2 Hz).
    {
        ChordaAudioProcessor processor;
        patch (processor);
        setParameter (processor, lfoAmount, 1.0f);
        setParameter (processor, lfoRate,   1.7f);
        report.check (processor.getLfoRateText().endsWith ("Hz"), "free, the rate reads in Hz", processor.getLfoRateText());

        setParameter (processor, lfoSync, 1.0f);
        report.check (processor.getLfoRateText() == "1/4", "synced, it reads as a note division", processor.getLfoRateText());

        std::vector<float> lfo;
        renderWatchingLfo (processor, note, seconds (3.0), lfo);
        const auto count = crossings (lfo);
        report.check (count >= 11 && count <= 13, "synced to a quarter at 120 BPM it runs at 2 Hz",
                      juce::String (count) + " crossings in 3 s");
    }
}

//==============================================================================
/** Note changes do not click in Mono (which steals its single voice) or in Legato
    without glide (which retunes the ringing loop). */
void testNoteChangesDoNotClick (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1y. Note changes in Mono and Legato do not click");

    struct Case { const char* mode; bool overlap; int from, to; };
    for (const auto& c : { Case { "Mono", true, 48, 55 }, Case { "Mono", false, 48, 55 },
                           Case { "Legato", true, 48, 55 }, Case { "Legato", false, 48, 55 },
                           Case { "Legato", true, 36, 48 }, Case { "Legato", true, 60, 43 } })
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode, (float) pluck::voiceModeNames.indexOf (c.mode));
        setParameter (processor, pitchGlide, 0.0f);
        setParameter (processor, stringSustain, 0.6f);
        setParameter (processor, stringDecay, 4.0f);

        // Overlapping keys, or the first let go just before the second.
        const int change = seconds (0.6) + 37;
        std::vector<MidiEvent> events { { 0, juce::MidiMessage::noteOn (1, c.from, 0.9f) },
                                        { change, juce::MidiMessage::noteOn (1, c.to, 0.9f) } };
        events.push_back ({ c.overlap ? change + seconds (0.3) : change - seconds (0.02), juce::MidiMessage::noteOff (1, c.from) });
        const auto audio = render (processor, events, seconds (1.2));

        const auto name = juce::String (c.mode) + " " + juce::String (c.from) + " to " + juce::String (c.to)
                        + (c.overlap ? ", keys overlapping" : ", first key let go");
        writeWav (outDir, "01y_" + name.replace (" ", "_").replace (",", ""), audio);

        // Largest sample step across the change, relative to the largest step just before it.
        const auto* x = audio.getReadPointer (0);
        auto maxStep = [&] (int from, int to)
        {
            float m = 0.0f;
            for (int i = from; i < to; ++i)
                m = juce::jmax (m, std::abs (x[i] - x[i - 1]));
            return m;
        };
        const auto stepDb = juce::Decibels::gainToDecibels (maxStep (change - seconds (0.021), change + seconds (0.01))
                                                            / juce::jmax (1.0e-9f, maxStep (change - seconds (0.1), change - seconds (0.03))));
        const auto clicks = countClicks (audio, change - seconds (0.05), change + seconds (0.05));

        report.check (stepDb < 3.0f && clicks == 0, name + ": no click",
                      juce::String (stepDb, 1) + " dB step, " + juce::String (clicks) + " clicks");
    }
}

} // namespace chorda_test
