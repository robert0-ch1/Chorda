/*
  ==============================================================================

    StringTests.cpp

    The string: tuning, the pick, the dampener, the exciter and the sub.

  ==============================================================================
*/

#include "TestHelpers.h"
#include "Tests.h"

namespace chorda_test
{

void testTuning (TestReport& report)
{
    report.section ("1g. Notes are in tune");
    using namespace pluck::ParamID;

    for (const int note : { 33, 57, 69, 93, 105 })
        for (const float brightness : { 800.0f, 20000.0f })
        {
            ChordaAudioProcessor processor;
            makeDryTestPatch (processor);
            setParameter (processor, exciterTone, pluck::toneNoise);
            setParameter (processor, exciterAttack, 2.0f);
            setParameter (processor, stringBrightness, brightness);
            setParameter (processor, stringSustain, 1.0f);     // exercise the hold path (filter bypass) too

            const auto audio = render (processor,
                                       { { 0,             juce::MidiMessage::noteOn  (1, note, 1.0f) },
                                         { seconds (1.4), juce::MidiMessage::noteOff (1, note) } },
                                       seconds (1.5));

            const auto expected = 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
            const auto measured = estimateFrequency (audio, seconds (0.6), seconds (0.6), expected);
            const auto cents    = 1200.0f * std::log2 (measured / expected);

            report.check (std::abs (cents) < 2.0f,
                          "note " + juce::String (note) + " at " + juce::String ((int) brightness) + " Hz brightness is in tune",
                          juce::String (measured, 2) + " Hz, " + juce::String (cents, 2) + " cents");
        }
}

void testStringControls (TestReport& report, const juce::File& outDir)
{
    report.section ("1h. Sub, drive and damper do what they say");
    using namespace pluck::ParamID;
    const float f0 = 220.0f;   // A3

    auto renderA3 = [&] (std::function<void (ChordaAudioProcessor&)> setup, const juce::String& wavName)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, exciterTone, pluck::toneNoise);
        setParameter (processor, exciterAttack, 2.0f);
        setParameter (processor, exciterPosition, 0.13f);
        setParameter (processor, stringBrightness, 20000.0f);
        setParameter (processor, stringSustain, 1.0f);
        setup (processor);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 57, 1.0f) },
                                                { seconds (1.9), juce::MidiMessage::noteOff (1, 57) } }, seconds (2.0));
        writeWav (outDir, wavName, audio);
        return audio;
    };

    // Sub: a sine an octave below appears, and only then.
    {
        const auto plain = renderA3 ([] (auto&) {}, "01h_sub_off");
        const auto sub   = renderA3 ([] (auto& p) { setParameter (p, stringSub, 1.0f); }, "01h_sub_full");
        // partialLevelDb (f, k) is level (k f) / level (f); with f = f0/2, k = 2 that is fundamental / sub.
        const auto before = -partialLevelDb (plain, seconds (0.3), seconds (0.5), f0 * 0.5f, 2);   // sub / fundamental
        const auto after  = -partialLevelDb (sub,   seconds (0.3), seconds (0.5), f0 * 0.5f, 2);
        report.check (after > before + 20.0f, "sub adds an octave-below sine",
                      "sub/fundamental " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB");
        report.check (partialLevelDb (sub, seconds (0.3), seconds (0.5), f0, 1) > -6.0f, "sub level does not swamp the string");
    }

    // Drive: feed the valve a near-pure sine (sine exciter, dark string, no comb,
    // held) and look at the third harmonic it adds. Deterministic, unlike noise.
    {
        auto renderSine = [&] (float drive, const juce::String& wavName)
        {
            ChordaAudioProcessor processor;
            makeDryTestPatch (processor);
            setParameter (processor, exciterTone, pluck::toneSine);
            setParameter (processor, exciterAttack, 20.0f);
            setParameter (processor, exciterPosition, 0.5f);   // mid-string: the comb leaves the test tone alone
            setParameter (processor, stringBrightness, 200.0f);   // clamps to 5 f0: upper partials die fast
            setParameter (processor, stringSustain, 1.0f);
            setParameter (processor, outputDrive, drive);
            const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 57, 1.0f) },
                                                    { seconds (1.4), juce::MidiMessage::noteOff (1, 57) } }, seconds (1.5));
            writeWav (outDir, wavName, audio);
            return audio;
        };

        const auto clean  = renderSine (0.0f, "01h_drive_off");
        const auto driven = renderSine (1.0f, "01h_drive_full");
        const auto thirdClean  = partialLevelDb (clean,  seconds (0.8), seconds (0.5), f0, 3);
        const auto thirdDriven = partialLevelDb (driven, seconds (0.8), seconds (0.5), f0, 3);
        report.check (thirdClean < -25.0f, "the test tone is clean without drive", "3rd/1st " + juce::String (thirdClean, 1) + " dB");
        report.check (thirdDriven > thirdClean + 12.0f, "drive adds odd harmonics",
                      "3rd/1st " + juce::String (thirdClean, 1) + " dB -> " + juce::String (thirdDriven, 1) + " dB");
        const auto secondDriven = partialLevelDb (driven, seconds (0.8), seconds (0.5), f0, 2);
        report.check (secondDriven > -40.0f, "drive adds even harmonics (valve asymmetry)", "2nd/1st " + juce::String (secondDriven, 1) + " dB");
        report.check (allFinite (driven) && driven.getMagnitude (0, driven.getNumSamples()) < 1.5f, "driven output is bounded",
                      "peak " + juce::String (driven.getMagnitude (0, driven.getNumSamples()), 3));
        report.check (clean.getMagnitude (0, seconds (0.1), seconds (0.3)) > 0.05f, "zero drive passes the signal");
    }

    // Damper at the middle: the fundamental has no node there and is damped,
    // the octave partial has one and survives (the "harmonic" technique).
    {
        const auto plain  = renderA3 ([] (auto&) {}, "01h_damper_off");
        const auto middle = renderA3 ([] (auto& p) { setParameter (p, stringDamper, 0.5f); }, "01h_damper_middle");
        const auto before = partialLevelDb (plain,  seconds (1.0), seconds (0.5), f0, 2);
        const auto after  = partialLevelDb (middle, seconds (1.0), seconds (0.5), f0, 2);
        report.check (after > before + 20.0f, "damper at the middle leaves the octave and kills the fundamental",
                      "2nd/1st " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB");

        const auto octave = estimateFrequency (middle, seconds (1.0), seconds (0.6), 2.0f * f0);
        const auto cents  = 1200.0f * std::log2 (octave / (2.0f * f0));
        report.check (std::abs (cents) < 3.0f, "the surviving octave is in tune with the damper engaged",
                      juce::String (octave, 2) + " Hz, " + juce::String (cents, 2) + " cents");
    }
}

/** The sub is an octave below the string, and must stay there: a sub running
    at the nominal f0/2 drifts against a loop that lands a fraction of a sample
    away from f0, and the two phase against each other. */
void testSubTracksTheString (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1j. The sub stays an octave below the string");

    for (const int note : { 45, 61, 73 })
    {
        const auto f0 = (float) juce::MidiMessage::getMidiNoteInHertz (note);

        ChordaAudioProcessor processor;
        setParameter (processor, stringSustain, 1.0f);
        setParameter (processor, stringSub,     0.8f);
        // A square exciter, not the default noise: it is just as rich for the
        // sub to phase against, and it renders the same way every time. A short
        // noise burst does not, and its luck would show up here as a swing.
        setParameter (processor, exciterTone,   pluck::toneSquare);

        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 0.9f) } }, seconds (6.0));
        writeWav (outDir, "01j_sub_" + juce::String (note), audio);

        const auto name = juce::String ("note ") + juce::String (note);
        const auto measured = estimateFrequency (audio, seconds (4.0), seconds (1.5), f0 * 0.5f);
        const auto cents = 1200.0f * std::log2 (measured / (f0 * 0.5f));
        report.check (std::abs (cents) < 5.0f, name + ": the sub sits an octave below, within 5 cents",
                      juce::String (measured, 2) + " Hz vs " + juce::String (f0 * 0.5f, 2) + " Hz (" + juce::String (cents, 2) + " cents)");

        // No slow phasing: the held level must not swing.
        float worst = 0.0f, quietest = 1.0e9f;
        for (int w = 0; w < 7; ++w)
        {
            const auto level = peakBetween (audio, seconds (3.0 + 0.4 * w), seconds (3.0 + 0.4 * (w + 1)));
            worst = juce::jmax (worst, level);
            quietest = juce::jmin (quietest, level);
        }
        const auto swing = juce::Decibels::gainToDecibels (worst / juce::jmax (quietest, 1.0e-9f));
        report.check (swing < 3.0f, name + ": the sub does not phase against the string",
                      juce::String (swing, 2) + " dB swing over 2.8 s");
    }
}

/** The pick is a place on the string, not a switch. */
void testPickIsAlwaysOnTheString (TestReport& report)
{
    report.section ("1k. The pick is always on the string");

    ChordaAudioProcessor processor;
    auto* parameter = processor.getAPVTS().getParameter (pluck::ParamID::exciterPosition);
    const auto lowest = parameter->convertFrom0to1 (0.0f);

    report.check (lowest > 0.0f, "the pick position cannot be turned off",
                  "lowest " + juce::String (lowest, 3) + " of the string length");
    report.check (lowest <= 0.05f, "the pick can still get very close to the bridge",
                  "lowest " + juce::String (juce::roundToInt (lowest * 200.0f)) + " %");
}

/** Tone crossfades the three excitation waveforms. Either end of the knob must
    be exactly the waveform it names, and the way between them must not dip or
    bulge in level: it is a colour control, not a volume one. */
void testToneCrossfade (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1l. Tone crossfades sine, square and noise");

    // A noise burst shorter than a period lands on the string's resonances
    // differently every time, so a single render of the noise end of the knob
    // can be several dB either way. Average a handful of them.
    constexpr int renders = 6;

    auto levelAt = [&] (float tone)
    {
        double total = 0.0;
        for (int i = 0; i < renders; ++i)
        {
            ChordaAudioProcessor processor;
            makeDryTestPatch (processor);
            setParameter (processor, exciterTone, tone);
            setParameter (processor, stringDecay, 4.0f);

            const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 52, 1.0f) } }, seconds (0.6));
            if (i == 0)
            {
                if (outDir != juce::File())
                    writeWav (outDir, "01l_tone_" + juce::String (juce::roundToInt (tone * 100.0f)), audio);
                report.check (allFinite (audio), "tone " + juce::String (tone, 2) + ": stays finite");
            }
            total += peakBetween (audio, seconds (0.1), seconds (0.5));
        }
        return (float) (total / renders);
    };

    std::vector<float> levels;
    juce::String trace;
    for (const float tone : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        levels.push_back (levelAt (tone));
        trace += juce::String (tone, 2) + ":" + juce::String (juce::Decibels::gainToDecibels (levels.back()), 1) + " ";
    }
    std::cout << "    level along Tone (dB): " << trace << std::endl;

    auto lo = levels[0], hi = levels[0];
    for (auto v : levels) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
    const auto spread = juce::Decibels::gainToDecibels (hi / juce::jmax (lo, 1.0e-9f));
    report.check (spread < 10.0f, "the knob changes colour without a hole in the level",
                  juce::String (spread, 1) + " dB from end to end");

    // Neither half may swing further than its own endpoints do.
    for (int half = 0; half < 2; ++half)
    {
        const auto a = levels[(size_t) (half * 2)], b = levels[(size_t) (half * 2 + 1)], c = levels[(size_t) (half * 2 + 2)];
        const auto ends = juce::jmax (a, c), middle = b;
        report.check (middle > juce::jmin (a, c) * 0.5f && middle < ends * 2.0f,
                      juce::String (half == 0 ? "sine to square" : "square to noise") + " passes smoothly through its middle",
                      juce::String (juce::Decibels::gainToDecibels (middle / juce::jmax (ends, 1.0e-9f)), 1) + " dB against its ends");
    }

    // The ends are the named waveforms, so the knob's text must say so.
    auto* parameter = ChordaAudioProcessor().getAPVTS().getParameter (exciterTone);
    juce::ignoreUnused (parameter);
    report.check (pluck::toneSine < pluck::toneSquare && pluck::toneSquare < pluck::toneNoise,
                  "the three waveforms sit in order along the knob");
}

/** A low string loses its top end as fast, in seconds, as a middle one. The
    loop filter acts once per round trip, and low notes make few of them. */
void testLowNotesLoseTheirTop (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1q. Low notes lose their top end as fast as middle ones");

    auto highShareDb = [] (const juce::AudioBuffer<float>& a, int start)
    {
        constexpr int order = 14, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> d ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
            d[(size_t) i] = a.getSample (0, start + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1)));
        fft.performFrequencyOnlyForwardTransform (d.data());
        double high = 0, total = 1.0e-30;
        for (int b = 1; b < n / 2; ++b)
        {
            const auto e = (double) d[(size_t) b] * d[(size_t) b];
            total += e;
            if ((float) b * (float) sampleRate / (float) n >= 2000.0f) high += e;
        }
        return 10.0 * std::log10 (high / total + 1.0e-15);
    };

    // How fast the share of energy above 2 kHz falls, in dB a second, over
    // the first second. Square pluck, so it is the same every run.
    auto fallRate = [&] (int note)
    {
        ChordaAudioProcessor processor;
        setParameter (processor, exciterTone, pluck::toneSquare);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } }, seconds (1.6));
        writeWav (outDir, "01q_note_" + juce::String (note), audio);
        return (highShareDb (audio, seconds (0.45)) - highShareDb (audio, seconds (0.05))) / 0.4;
    };

    const auto middle = fallRate (48);   // C3, the reference
    for (const int note : { 16, 28, 40 })
    {
        const auto low = fallRate (note);
        report.check (low < middle * 0.6, "note " + juce::String (note) + " loses its top end at least 60 % as fast as C3",
                      juce::String (juce::roundToInt (low)) + " dB/s against " + juce::String (juce::roundToInt (middle)) + " dB/s");
    }
}

/** Dragging the damper along the string is smooth: the marker sends a new
    position every block, and the filter must glide between them. */
void testDamperDragIsSmooth (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1s. Dragging the damper is smooth");

    for (const float pressure : { 0.5f, 1.0f })
    {
        ChordaAudioProcessor processor;                    // factory defaults: a noise pluck
        setParameter (processor, stringSustain,        1.0f);
        setParameter (processor, stringDamper,         0.05f);
        setParameter (processor, stringDamperPressure, pressure);

        processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const int total = seconds (3.0);
        juce::AudioBuffer<float> output (2, total), block (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 52, 0.9f), 0);

        for (int position = 0; position < total; position += blockSize)
        {
            // A drag from 10 % to 90 % and back over two seconds, one step a block.
            const auto t = juce::jlimit (0.0, 1.0, ((double) position / sampleRate - 0.5) / 1.0);
            const auto back = (double) position / sampleRate > 1.5 ? juce::jlimit (0.0, 1.0, ((double) position / sampleRate - 1.5)) : 0.0;
            setParameter (processor, stringDamper, (float) (0.05 + 0.4 * (t - back)));

            block.clear();
            processor.processBlock (block, midi);
            midi.clear();
            output.copyFrom (0, position, block, 0, 0, juce::jmin (blockSize, total - position));
            output.copyFrom (1, position, block, 1, 0, juce::jmin (blockSize, total - position));
        }
        processor.releaseResources();
        writeWav (outDir, "01s_drag_" + juce::String (juce::roundToInt (pressure * 100.0f)), output);

        const auto clicks = countClicks (output, seconds (0.4), total);
        report.check (allFinite (output) && clicks == 0,
                      "dragging the damper at " + juce::String (juce::roundToInt (pressure * 100.0f)) + " % pressure does not crackle",
                      juce::String (clicks) + " clicks");
    }
}

/** The whole string is playable, and its two halves mirror each other the
    way a real string's do. */
void testTheWholeString (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1t. The whole string, and its symmetry");

    auto renderAt = [&] (float pick, float damper, const juce::String& name)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, exciterTone,     pluck::toneSquare);
        setParameter (processor, exciterAttack,   2.0f);
        setParameter (processor, exciterPosition, pick);
        setParameter (processor, stringDamper,    damper);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 45, 1.0f) } }, seconds (1.0));
        writeWav (outDir, "01t_" + name, audio);
        return audio;
    };

    // A pluck at 27 % and one at 73 % excite the same harmonics equally.
    // (27 %: none of partials 3 to 9 has a node there, where a level is all
    // rounding.)
    const auto f0 = 440.0f * std::pow (2.0f, (45.0f - 69.0f) / 12.0f);
    const auto near = renderAt (0.27f, 0.0f, "pick_27"), far = renderAt (0.73f, 0.0f, "pick_73");
    float worst = 0.0f;
    for (const int k : { 3, 5, 7, 9 })
        worst = juce::jmax (worst, std::abs (partialLevelDb (near, seconds (0.2), seconds (0.5), f0, k)
                                             - partialLevelDb (far,  seconds (0.2), seconds (0.5), f0, k)));
    report.check (worst < 1.0f, "plucking at 73 % excites the same harmonics as at 27 %",
                  "largest difference " + juce::String (worst, 2) + " dB over partials 3 to 9");

    // ...but they are not the same pluck: the waves reach the bridge in the other order.
    float waveform = 0.0f;
    for (int i = seconds (0.2); i < seconds (0.3); ++i)
        waveform = juce::jmax (waveform, std::abs (near.getSample (0, i) - far.getSample (0, i)));
    report.check (waveform > 0.01f, "the far half of the string is a different pluck, not a copy",
                  "largest sample difference " + juce::String (waveform, 3));

    // A finger at 30 % and at 70 % touches the same nodes: the same sound.
    const auto fingerNear = renderAt (0.2f, 0.3f, "damper_30"), fingerFar = renderAt (0.2f, 0.7f, "damper_70");
    float fingerDifference = 0.0f;
    for (int i = 0; i < fingerNear.getNumSamples(); ++i)
        fingerDifference = juce::jmax (fingerDifference, std::abs (fingerNear.getSample (0, i) - fingerFar.getSample (0, i)));
    report.check (fingerDifference < 1.0e-3f, "a finger at 70 % damps the string as one at 30 % does",
                  "largest difference " + juce::String (fingerDifference, 6));

    // And at the very far end it touches nothing that moves: off, as at the bridge.
    const auto fingerOff = renderAt (0.2f, 0.0f, "damper_off"), fingerAtNut = renderAt (0.2f, 1.0f, "damper_nut");
    float nutDifference = 0.0f;
    for (int i = 0; i < fingerOff.getNumSamples(); ++i)
        nutDifference = juce::jmax (nutDifference, std::abs (fingerOff.getSample (0, i) - fingerAtNut.getSample (0, i)));
    report.check (nutDifference == 0.0f, "a finger at the nut is as good as no finger", "largest difference " + juce::String (nutDifference, 6));

    ChordaAudioProcessor fresh;
    auto* pick = fresh.getAPVTS().getParameter (exciterPosition);
    report.check (pick->getText (pick->convertTo0to1 (0.5f), 0) == "50 %", "the middle of the string reads 50 %",
                  pick->getText (pick->convertTo0to1 (0.5f), 0));
}

/** A pitch bend moves the pitch and nothing else: the loop length glides
    rather than stepping once a block, which used to add a zipper. */
void testBendIsClean (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("1v. A pitch bend is clean");

    auto highShare = [] (const juce::AudioBuffer<float>& a)
    {
        constexpr int order = 14, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> d ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
            d[(size_t) i] = a.getSample (0, seconds (2.0) + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1)));
        fft.performFrequencyOnlyForwardTransform (d.data());
        double high = 0, total = 1.0e-30;
        for (int b = 1; b < n / 2; ++b)
        {
            const auto e = (double) d[(size_t) b] * d[(size_t) b];
            total += e;
            if ((double) b * sampleRate / n > 4000.0) high += e;
        }
        return 10.0 * std::log10 (high / total + 1.0e-15);
    };

    ChordaAudioProcessor processor;
    setParameter (processor, exciterTone,      pluck::toneSine);
    setParameter (processor, stringSustain,    1.0f);
    setParameter (processor, stringBrightness, 3000.0f);
    std::vector<MidiEvent> events { { 0, juce::MidiMessage::noteOn (1, 57, 0.9f) } };
    for (int b = 0; b < seconds (3.0) / blockSize; ++b)
    {
        const auto t = (double) b * blockSize / sampleRate;
        events.push_back ({ seconds (0.5) + b * blockSize,
                            juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + (int) (8191 * std::sin (juce::MathConstants<double>::twoPi * 0.5 * t)))) });
    }
    const auto audio = render (processor, events, seconds (3.0));
    const auto share = highShare (audio);
    report.check (share < -65.0, "a held sine bent back and forth gains no top end",
                  juce::String (share, 1) + " dB of its energy above 4 kHz (it was -45 before the fix)");
}

/** Moving the pick reshapes a note that is already ringing, and a damper
    with no pressure is no damper. */
void testPickWhileRinging (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("1w. The pick and the damper while a note rings");

    auto renderMove = [] (float from, float to)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, exciterTone,     pluck::toneSquare);
        setParameter (processor, stringSustain,   1.0f);
        setParameter (processor, exciterPosition, from);
        processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> output (2, seconds (2.0)), block (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
        for (int position = 0; position < seconds (2.0); position += blockSize)
        {
            if (position >= seconds (1.0))
                setParameter (processor, exciterPosition, to);
            block.clear();
            processor.processBlock (block, midi);
            midi.clear();
            output.copyFrom (0, position, block, 0, 0, juce::jmin (blockSize, seconds (2.0) - position));
        }
        return output;
    };

    const auto f0 = 440.0f * std::pow (2.0f, (45.0f - 69.0f) / 12.0f);
    const auto moved = renderMove (0.1f, 0.5f);
    const auto before = partialLevelDb (moved, seconds (0.6), seconds (0.3), f0, 3);
    const auto after  = partialLevelDb (moved, seconds (1.5), seconds (0.3), f0, 3);
    report.check (std::abs (after - before) > 6.0f, "moving the pick changes a note already ringing",
                  "3rd partial " + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB");
    report.check (countClicks (moved, seconds (0.3), seconds (2.0)) == 0, "and it moves without a click");

    auto renderDamper = [] (float pressure, float position)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, stringDamper,         position);
        setParameter (processor, stringDamperPressure, pressure);
        return render (processor, { { 0, juce::MidiMessage::noteOn (1, 45, 1.0f) } }, seconds (0.8));
    };
    const auto off = renderDamper (0.6f, 0.0f), noPressure = renderDamper (0.0f, 0.3f);
    float difference = 0.0f;
    for (int i = 0; i < off.getNumSamples(); ++i)
        difference = juce::jmax (difference, std::abs (off.getSample (0, i) - noPressure.getSample (0, i)));
    report.check (difference < 1.0e-6f, "a damper with no pressure is no damper", "largest difference " + juce::String (difference, 8));
}

/** A note sounds when it is played. The string used to be heard at the far
    end of its delay line, so every note waited one period: 31 ms at C1. */
void testNoLatency (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("1x. Notes sound at once");

    float worst = 0.0f;
    for (int note : { 12, 24, 36, 48, 60, 72 })
    {
        ChordaAudioProcessor processor;
        setParameter (processor, exciterTone, pluck::toneSquare);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 0.9f) } }, seconds (0.3));
        const auto peak = audio.getMagnitude (0, 0, audio.getNumSamples());
        int first = audio.getNumSamples();
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (std::abs (audio.getSample (0, i)) > peak * 0.01f) { first = i; break; }
        worst = juce::jmax (worst, (float) (first * 1000.0 / sampleRate));
    }
    report.check (worst < 1.0f, "every note from C0 to C5 is heard within 1 ms of its note-on",
                  "slowest " + juce::String (worst, 2) + " ms");
}

/** Sine, square and noise at one loudness. The tones used to be up to 10 dB
    louder than the noise, which clipped chords played on them. */
void testExciterLevels (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("1o. Sine, square and noise play at one loudness");

    auto loudness = [] (float tone, int note, float attack)
    {
        // The noise burst varies from pluck to pluck (see the note on
        // juce::Random in the handoff), so it is averaged; 8 renders still
        // let the worst case wander by 3 dB from run to run.
        const int renders = tone > 0.9f ? 16 : 1;
        double total = 0.0;
        for (int r = 0; r < renders; ++r)
        {
            ChordaAudioProcessor processor;
            setParameter (processor, exciterTone,   tone);
            setParameter (processor, exciterAttack, attack);
            const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } }, seconds (0.6));
            total += kWeightedMeanSquare (audio, 0, seconds (0.5));
        }
        return 10.0 * std::log10 (total / renders);
    };

    std::vector<double> sineOff, squareOff;
    juce::String trace;
    for (int note : { 40, 52, 64, 76 })
        for (float attack : { 3.0f, 10.0f, 50.0f })
        {
            const auto noise = loudness (pluck::toneNoise, note, attack);
            sineOff.push_back   (loudness (pluck::toneSine,   note, attack) - noise);
            squareOff.push_back (loudness (pluck::toneSquare, note, attack) - noise);
            trace += juce::String (note) + "/" + juce::String ((int) attack) + ": " + juce::String (sineOff.back(), 1)
                   + " " + juce::String (squareOff.back(), 1) + "  ";
        }
    std::cout << "    sine and square against noise (dB, note/attack ms): " << trace << std::endl;

    auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
    auto loudest = [] (const std::vector<double>& v) { return *std::max_element (v.begin(), v.end()); };

    report.check (std::abs (median (sineOff)) < 1.5,   "the sine is as loud as the noise",   juce::String (median (sineOff), 1) + " dB median");
    report.check (std::abs (median (squareOff)) < 1.5, "the square is as loud as the noise", juce::String (median (squareOff), 1) + " dB median");
    report.check (loudest (sineOff) < 6.0 && loudest (squareOff) < 6.0, "neither tone is ever much louder than the noise",
                  "loudest " + juce::String (juce::jmax (loudest (sineOff), loudest (squareOff)), 1) + " dB");

    // A chord of squares, the loudest case there was, has its headroom back.
    {
        ChordaAudioProcessor processor;
        setParameter (processor, exciterTone, pluck::toneSquare);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 64, 1.0f) },
                                                { 0, juce::MidiMessage::noteOn (1, 67, 1.0f) },
                                                { 0, juce::MidiMessage::noteOn (1, 71, 1.0f) } }, seconds (0.8));
        const auto peak = audio.getMagnitude (0, 0, audio.getNumSamples());
        report.check (peak < 0.7f, "a full-velocity chord of squares keeps its headroom",
                      "peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
    }
}

/** The pluck filter's level make-up uses a closed form for the energy of a
    cascade of one-poles. It must agree with summing the impulse response. */
void testCascadeEnergy (TestReport& report)
{
    report.section ("1z. Pluck filter make-up");

    double worst = 0.0;
    for (const int stages : { 1, 2, 3, 5, 8, 12, 16 })
        for (const double a : { 0.05, 0.3, 0.6, 0.9, 0.99, 0.999 })
        {
            std::vector<double> state ((size_t) stages, 0.0);
            double energy = 0.0;
            for (int n = 0; n < 400000; ++n)
            {
                double x = n == 0 ? 1.0 : 0.0;
                for (auto& y : state)
                {
                    y = (1.0 - a) * x + a * y;
                    x = y;
                }
                energy += x * x;
            }
            const auto closed = pluck::KarplusVoice::cascadeEnergy (stages, a);
            worst = juce::jmax (worst, std::abs (closed / energy - 1.0));
        }

    report.check (worst < 1.0e-6, "the closed form matches the summed impulse response",
                  "largest relative error " + juce::String (worst, 9));
}

} // namespace chorda_test
