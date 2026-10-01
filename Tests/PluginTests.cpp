/*
  ==============================================================================

    PluginTests.cpp

    The plugin as a whole: presets, state, sample rate, width, reverb and safety.

  ==============================================================================
*/

#include "TestHelpers.h"
#include "Tests.h"

namespace chorda_test
{

/** Every factory preset loads cleanly and renders finite, audible, bounded audio. */
void testFactoryPresets (TestReport& report, const juce::File& outDir)
{
    report.section ("4. Factory presets render");

    ChordaAudioProcessor processor;
    auto& presets = processor.getPresetManager();

    report.check (presets.getNumFactoryPresets() > 1, "there is more than one factory preset",
                  juce::String (presets.getNumFactoryPresets()) + " presets");

    for (int i = 0; i < presets.getNumFactoryPresets(); ++i)
    {
        presets.loadFactoryPreset (i);
        const auto name = presets.getFactoryPresetName (i);

        report.check (presets.getCurrentPresetName() == name, "preset name is set after loading: " + name);
        report.check (! presets.isModified(), "preset is not marked modified right after loading: " + name);

        const auto audio = render (processor,
                                   { { 0,             juce::MidiMessage::noteOn  (1, 60, 0.8f) },
                                     { seconds (0.6), juce::MidiMessage::noteOff (1, 60) } },
                                   seconds (1.2));
        writeWav (outDir, "04_preset_" + juce::String (i).paddedLeft ('0', 2) + "_" + name.replaceCharacter (' ', '_'), audio);

        const auto peak = audio.getMagnitude (0, audio.getNumSamples());
        report.check (allFinite (audio) && peak > 1.0e-3f && peak < 4.0f,
                      "renders finite, audible, bounded audio: " + name, "peak " + juce::String (peak, 3));
    }

    setParameter (processor, pluck::ParamID::stringDecay, 1.234f);
    report.check (presets.isModified(), "editing a parameter marks the preset modified");
}

/** Every parameter and the preset name survive getStateInformation / setStateInformation. */
void testStateRoundTrip (TestReport& report)
{
    report.section ("5. Host state round trip");
    using namespace pluck::ParamID;

    ChordaAudioProcessor source;
    source.getPresetManager().loadFactoryPreset (2);
    setParameter (source, exciterTone, 2.0f);
    setParameter (source, stringBrightness, 1234.0f);
    setParameter (source, stringRelease, 0.777f);
    setParameter (source, stringSustain, 0.42f);
    setParameter (source, stringSub, 0.33f);
    setParameter (source, outputDrive, 0.6f);
    setParameter (source, pitchOctave, 3.0f);
    setParameter (source, stringDamperPressure, 0.9f);
    setParameter (source, voiceMode, (float) pluck::voiceModeMono);
    setParameter (source, outputGain, -6.5f);

    juce::MemoryBlock state;
    source.getStateInformation (state);
    report.check (state.getSize() > 0, "state is not empty", juce::String ((int) state.getSize()) + " bytes");

    ChordaAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    for (const auto& id : pluck::ParamID::all)
    {
        const auto a = getParameter (source,   id.toRawUTF8());
        const auto b = getParameter (restored, id.toRawUTF8());
        report.check (std::abs (a - b) < 1.0e-4f, "parameter restored: " + id,
                      juce::String (a, 4) + " vs " + juce::String (b, 4));
    }

    report.check (restored.getPresetManager().getCurrentPresetName() == source.getPresetManager().getCurrentPresetName(),
                  "preset name restored", restored.getPresetManager().getCurrentPresetName());
    report.check (! restored.getPresetManager().isModified(), "restored state is not marked modified");
}

/** The processor renders at 44.1 and 96 kHz after being prepared at each. */
void testSampleRateChange (TestReport& report)
{
    report.section ("6. Sample-rate change");

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);

    const std::vector<MidiEvent> events { { 0,     juce::MidiMessage::noteOn  (1, 69, 1.0f) },
                                          { 20000, juce::MidiMessage::noteOff (1, 69) } };

    const auto at44 = render (processor, events, 44100, 44100.0);
    const auto at96 = render (processor, events, 96000, 96000.0);

    report.check (allFinite (at44) && at44.getMagnitude (0, at44.getNumSamples()) > 0.01f, "renders at 44.1 kHz");
    report.check (allFinite (at96) && at96.getMagnitude (0, at96.getNumSamples()) > 0.01f, "renders at 96 kHz");
}

/** Width 0 is bit-exact mono, width 1 decorrelates within 3 dB of the mono level, 0.5 sits between. */
void testStereoWidth (TestReport& report)
{
    report.section ("7. Stereo width");
    using namespace pluck::ParamID;

    const std::vector<MidiEvent> chord { { 0, juce::MidiMessage::noteOn (1, 52, 0.9f) },
                                         { 0, juce::MidiMessage::noteOn (1, 59, 0.9f) },
                                         { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) },
                                         { seconds (1.5), juce::MidiMessage::noteOff (1, 52) },
                                         { seconds (1.5), juce::MidiMessage::noteOff (1, 59) },
                                         { seconds (1.5), juce::MidiMessage::noteOff (1, 64) } };

    auto correlation = [] (const juce::AudioBuffer<float>& b)
    {
        double ll = 0.0, rr = 0.0, lr = 0.0;
        const auto* l = b.getReadPointer (0);
        const auto* r = b.getReadPointer (1);
        for (int i = 0; i < b.getNumSamples(); ++i) { ll += l[i] * l[i]; rr += r[i] * r[i]; lr += l[i] * r[i]; }
        return ll > 0.0 && rr > 0.0 ? lr / std::sqrt (ll * rr) : 1.0;
    };

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);
    setParameter (processor, outputWidth, 0.0f);
    const auto mono = render (processor, chord, seconds (2.0));

    float maxDifference = 0.0f;
    for (int i = 0; i < mono.getNumSamples(); ++i)
        maxDifference = juce::jmax (maxDifference, std::abs (mono.getSample (0, i) - mono.getSample (1, i)));
    report.check (maxDifference == 0.0f, "width 0: left and right are identical");

    setParameter (processor, outputWidth, 1.0f);
    const auto wide = render (processor, chord, seconds (2.0));
    report.check (allFinite (wide), "width 1: output is finite");

    const auto corr = correlation (wide);
    report.check (corr < 0.9, "width 1: channels are decorrelated (correlation " + juce::String (corr, 2) + ")");

    const auto monoRms = mono.getRMSLevel (0, 0, mono.getNumSamples());
    const auto wideRms = 0.5f * (wide.getRMSLevel (0, 0, wide.getNumSamples()) + wide.getRMSLevel (1, 0, wide.getNumSamples()));
    const auto ratioDb = juce::Decibels::gainToDecibels (wideRms / juce::jmax (1.0e-6f, monoRms));
    report.check (std::abs (ratioDb) < 3.0f, "width 1: level within 3 dB of mono (" + juce::String (ratioDb, 1) + " dB)");

    setParameter (processor, outputWidth, 0.5f);
    const auto half = render (processor, chord, seconds (2.0));
    const auto halfCorr = correlation (half);
    report.check (halfCorr > corr && halfCorr < 1.0, "width 0.5: between mono and full width (correlation " + juce::String (halfCorr, 2) + ")");
}

/** Output never exceeds +6 dBFS, the limiter is transparent below its ceiling, and
    random playing never makes the string run away. */
void testSafety (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("9. Safety");

    // Ceiling: eight-note chord at full drive and +12 dB gain.
    {
        ChordaAudioProcessor processor;
        setParameter (processor, outputGain,  12.0f);
        setParameter (processor, outputDrive, 1.0f);
        setParameter (processor, outputWidth, 1.0f);
        std::vector<MidiEvent> chord;
        for (int n : { 40, 44, 47, 52, 56, 59, 64, 68 })
            chord.push_back ({ 0, juce::MidiMessage::noteOn (1, n, 1.0f) });
        const auto audio = render (processor, chord, seconds (1.0));
        const auto peak = juce::jmax (audio.getMagnitude (0, 0, audio.getNumSamples()), audio.getMagnitude (1, 0, audio.getNumSamples()));
        report.check (allFinite (audio) && peak <= 1.9954f, "nothing leaves louder than +6 dBFS",
                      "peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
    }

    // Below the ceiling the limiter is transparent: width 0 stays bit-exact mono.
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 57, 0.8f) } }, seconds (0.5));
        float difference = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            difference = juce::jmax (difference, std::abs (audio.getSample (0, i) - audio.getSample (1, i)));
        report.check (difference == 0.0f && audio.getMagnitude (0, 0, audio.getNumSamples()) < 1.0f,
                      "below the ceiling the limiter is not there");
    }

    // 100 runs of 6 s: a random knob every 100 ms, random bends, occasional extra notes.
    // The string level (pre-limiter) must stay below 3; a healthy string stays under 1.
    {
        juce::Random rng (99);
        const char* ids[] = { exciterTone, exciterAttack, exciterPosition, stringDecay, stringSustain, stringRelease, stringBrightness,
                              stringDamper, stringDamperPressure, stringSub, pitchOctave, pitchGlide, lfoAmount, lfoRate,
                              lfoPressureAmount, lfoPressureRate, outputDrive, outputWidth };
        int runaways = 0;
        float loudest = 0.0f;
        for (int run = 0; run < 100; ++run)
        {
            ChordaAudioProcessor processor;
            setParameter (processor, stringSustain, rng.nextBool() ? 1.0f : rng.nextFloat());
            processor.setPlayConfigDetails (0, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
            juce::AudioBuffer<float> block (2, blockSize);
            float worst = 0.0f;
            bool finite = true;
            for (int position = 0; position < seconds (6.0); position += blockSize)
            {
                juce::MidiBuffer midi;
                if (position == 0 || (position % seconds (1.0) < blockSize && rng.nextBool()))
                    midi.addEvent (juce::MidiMessage::noteOn (1, rng.nextInt (110), 0.9f), 0);
                if (rng.nextInt (8) == 0)
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), 0);
                if (position % seconds (0.1) < blockSize)
                    processor.getAPVTS().getParameter (ids[rng.nextInt (18)])->setValueNotifyingHost (rng.nextFloat());

                block.clear();
                processor.processBlock (block, midi);
                finite = finite && allFinite (block);
                worst = juce::jmax (worst, processor.getStringLevel());
            }
            loudest = juce::jmax (loudest, worst);
            if (! finite || worst > 3.0f)
                ++runaways;
        }
        report.check (runaways == 0, "a hundred runs of random playing and knob turning never run away",
                      juce::String (runaways) + " runaways, loudest string " + juce::String (loudest, 2));
    }
}

/** Reverb send, last in the chain: zero is bit-exact, the tail rings on, loudness
    follows the knob, the dry path is untouched and the tail is stereo. */
void testReverb (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("8. Reverb");

    auto renderWith = [&] (float amount, const juce::String& name)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, stringDecay,   1.5f);
        setParameter (processor, stringRelease, 0.05f);
        setParameter (processor, outputReverb,  amount);
        const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, 52, 1.0f) },
                                                { seconds (0.8), juce::MidiMessage::noteOff (1, 52) } }, seconds (2.5));
        writeWav (outDir, "08_reverb_" + name, audio);
        return audio;
    };

    const auto dry  = renderWith (0.0f, "off");
    const auto half = renderWith (0.5f, "half_send");
    const auto full = renderWith (1.0f, "full_send");

    report.check (allFinite (half) && allFinite (full), "the reverb stays finite");

    float difference = 0.0f;
    for (int i = 0; i < dry.getNumSamples(); ++i)
        difference = juce::jmax (difference, std::abs (dry.getSample (0, i) - dry.getSample (1, i)));
    report.check (difference == 0.0f, "at zero the reverb is not there: width 0 stays mono, bit for bit");

    const auto dryTail  = peakBetween (dry,  seconds (1.2), seconds (1.6));
    const auto halfTail = peakBetween (half, seconds (1.2), seconds (1.6));
    report.check (dryTail < 1.0e-4f && halfTail > 1.0e-3f, "the room rings on after the string has stopped",
                  "tail " + juce::String (juce::Decibels::gainToDecibels (halfTail, -120.0f), 1) + " dB, dry "
                  + juce::String (juce::Decibels::gainToDecibels (dryTail, -120.0f), 1) + " dB");

    const auto dryLoud  = 10.0 * std::log10 (kWeightedMeanSquare (dry,  0, seconds (1.0)));
    const auto halfLoud = 10.0 * std::log10 (kWeightedMeanSquare (half, 0, seconds (1.0)));
    const auto fullLoud = 10.0 * std::log10 (kWeightedMeanSquare (full, 0, seconds (1.0)));
    std::cout << "    loudness against dry: half send " << juce::String (halfLoud - dryLoud, 1)
              << " dB, full send " << juce::String (fullLoud - dryLoud, 1) << " dB" << std::endl;
    report.check (fullLoud > halfLoud && halfLoud > dryLoud && fullLoud - dryLoud < 6.0,
                  "the send adds the room on top of the dry string, in step with the knob",
                  juce::String (halfLoud - dryLoud, 1) + " dB at half, " + juce::String (fullLoud - dryLoud, 1) + " dB at full");

    // Before the first reflection (4 ms) the output equals the dry signal.
    float earlyDifference = 0.0f;
    for (int i = 0; i < seconds (0.004); ++i)
        earlyDifference = juce::jmax (earlyDifference, std::abs (dry.getSample (0, i) - full.getSample (0, i)));
    report.check (earlyDifference < 1.0e-4f, "the dry string passes a full send untouched",
                  "largest difference in the first 4 ms " + juce::String (earlyDifference, 6));

    double lr = 0, ll = 0, rr = 0;
    for (int i = seconds (0.2); i < seconds (1.5); ++i)
    {
        const double l = full.getSample (0, i), r = full.getSample (1, i);
        lr += l * r; ll += l * l; rr += r * r;
    }
    const auto corr = lr / std::sqrt (ll * rr + 1.0e-20);
    report.check (corr < 0.8, "the room is stereo even on a mono string", "correlation " + juce::String (corr, 2));
}

} // namespace chorda_test
