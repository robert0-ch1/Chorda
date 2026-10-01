/*
  ==============================================================================

    Tools.cpp

    Command-line tools: the README screenshot, a tone measurement and two stress tests.

  ==============================================================================
*/

#include "TestHelpers.h"
#include "Tests.h"

namespace chorda_test
{

/** The string view inside an editor, wherever it sits in the tree. */
pluck::ui::StringView* findStringView (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* view = dynamic_cast<pluck::ui::StringView*> (child))
            return view;
        if (auto* found = findStringView (*child))
            return found;
    }
    return nullptr;
}

int saveEditorScreenshot (const juce::File& file, int pluckFrame)
{
    ChordaAudioProcessor processor;
    auto& presets = processor.getPresetManager();          // something more interesting than "Init"
    presets.loadPresetAtIndex (juce::jmax (0, presets.getAllPresetNames().indexOf ("Nylon Guitar")));

    // Play a chord for a moment so the string drawing is alive.
    render (processor, { { 0, juce::MidiMessage::noteOn (1, 52, 0.9f) },
                         { 0, juce::MidiMessage::noteOn (1, 59, 0.9f) },
                         { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) } }, seconds (0.05));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "Could not create the editor" << std::endl;
        return 1;
    }

    // Optionally hold the pick hand on one frame of its pluck.
    if (pluckFrame >= 0)
        if (auto* view = findStringView (*editor))
            view->showPluckFrameForSnapshot (pluckFrame);

    // Paint at 2x for a crisp image on high-DPI displays.
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::cerr << "Could not write " << file.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "Fonts: " << pluck::ui::fonts::describe() << std::endl;
    std::cout << "Saved " << image.getWidth() << "x" << image.getHeight() << " screenshot to "
              << file.getFullPathName() << std::endl;
    return 0;
}

int measureTone()
{
    using namespace pluck::ParamID;
    std::cout << "note attack | sine peak/rms | square peak/rms | noise peak/rms (dB)" << std::endl;
    for (int note : { 28, 40, 52, 64, 76, 88 })
        for (float attack : { 1.0f, 3.0f, 10.0f, 50.0f, 300.0f })
        {
            juce::String line = juce::String (note) + " " + juce::String (attack) + "ms |";
            for (float tone : { 0.0f, 0.5f, 1.0f })
            {
                double pk = 0, rms = 0;
                const int n = tone > 0.9f ? 8 : 1;
                for (int r = 0; r < n; ++r)
                {
                    ChordaAudioProcessor processor;
                    setParameter (processor, exciterTone, tone);
                    setParameter (processor, exciterAttack, attack);
                    const auto audio = render (processor, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } }, seconds (0.8));
                    pk += audio.getMagnitude (0, 0, audio.getNumSamples());
                    rms += kWeightedMeanSquare (audio, 0, seconds (0.5));
                }
                line += " " + juce::String (juce::Decibels::gainToDecibels ((float) (pk / n)), 1) + "/" + juce::String (10.0 * std::log10 (rms / n), 1) + " |";
            }
            std::cout << line << std::endl;
        }
    return 0;
}

int fuzz (int count)
{
    using namespace pluck::ParamID;
    juce::Random rng (1234);
    int bad = 0;
    for (int run = 0; run < count; ++run)
    {
        ChordaAudioProcessor p;
        juce::String desc;
        auto set = [&] (const char* id, float v) { setParameter (p, id, v); desc << id << "=" << juce::String (v, 3) << " "; };
        set (exciterTone, rng.nextFloat());
        set (exciterAttack, std::pow (10.0f, rng.nextFloat() * 3.3f - 0.3f));
        set (exciterPosition, 0.005f + 0.99f * rng.nextFloat());
        set (stringDecay, std::pow (10.0f, rng.nextFloat() * 2.6f - 1.3f));
        set (stringSustain, rng.nextBool() ? 1.0f : rng.nextFloat());
        set (stringRelease, std::pow (10.0f, rng.nextFloat() * 3.0f - 2.0f));
        set (stringBrightness, 200.0f * std::pow (100.0f, rng.nextFloat()));
        set (stringDamper, rng.nextBool() ? 0.0f : rng.nextFloat());
        set (stringDamperPressure, rng.nextFloat());
        set (stringSub, rng.nextFloat() * 0.5f);
        set (pitchOctave, (float) rng.nextInt (5));
        set (pitchGlide, rng.nextBool() ? 0.001f : 0.05f + rng.nextFloat() * 0.5f);
        set (voiceMode, (float) rng.nextInt (12));
        set (lfoAmount, rng.nextBool() ? 0.0f : rng.nextFloat());
        set (lfoRate, 0.1f + rng.nextFloat() * 15.0f);
        set (lfoPressureAmount, rng.nextBool() ? 0.0f : rng.nextFloat());
        set (lfoPressureRate, 0.1f + rng.nextFloat() * 15.0f);
        set (outputDrive, rng.nextFloat() * 0.5f);

        std::vector<MidiEvent> ev;
        const int n1 = rng.nextInt (100), n2 = rng.nextInt (100);
        desc << "notes " << n1 << " " << n2;
        ev.push_back ({ 0, juce::MidiMessage::noteOn (1, n1, 0.3f + 0.7f * rng.nextFloat()) });
        ev.push_back ({ seconds (0.6), juce::MidiMessage::noteOn (1, n2, 0.3f + 0.7f * rng.nextFloat()) });
        const bool bend = rng.nextBool();
        if (bend)
        {
            desc << " bend";
            for (int k = 0; k < 60; ++k)
                ev.push_back ({ seconds (0.7 + 0.02 * k), juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + (int) (8191 * std::sin (k * 0.3)))) });
        }
        ev.push_back ({ seconds (2.0), juce::MidiMessage::noteOff (1, n1) });
        ev.push_back ({ seconds (2.2), juce::MidiMessage::noteOff (1, n2) });
        const auto a = render (p, ev, seconds (3.5));
        float peak = 0.0f; bool finite = allFinite (a);
        for (int ch = 0; ch < 2; ++ch) peak = juce::jmax (peak, a.getMagnitude (ch, 0, a.getNumSamples()));
        if (! finite || peak > 3.0f)
        {
            ++bad;
            std::cout << "BAD run " << run << " peak " << peak << (finite ? "" : " NaN") << "  " << desc << std::endl;
        }
    }
    std::cout << bad << " bad of " << count << std::endl;
    return 0;
}

int fuzzLive (int count)
{
    using namespace pluck::ParamID;
    juce::Random rng (99);
    const char* ids[] = { exciterTone, exciterAttack, exciterPosition, stringDecay, stringSustain, stringRelease, stringBrightness,
                          stringDamper, stringDamperPressure, stringSub, pitchOctave, pitchGlide, lfoAmount, lfoRate,
                          lfoPressureAmount, lfoPressureRate, outputDrive, outputWidth };
    int bad = 0;
    for (int run = 0; run < count; ++run)
    {
        ChordaAudioProcessor p;
        setParameter (p, stringSustain, rng.nextBool() ? 1.0f : rng.nextFloat());
        setParameter (p, stringDecay, 20.0f * rng.nextFloat());
        p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> block (2, blockSize);
        juce::String log;
        float worst = 0.0f; bool finite = true;
        const int total = seconds (6.0);
        std::vector<float> perSecond (6, 0.0f);
        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::MidiBuffer midi;
            if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, rng.nextInt (110), 0.9f), 0);
            if (pos % seconds (1.0) < blockSize && pos > 0 && rng.nextBool()) midi.addEvent (juce::MidiMessage::noteOn (1, rng.nextInt (110), 0.9f), 0);
            if (rng.nextInt (8) == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), 0);
            if (pos % seconds (0.1) < blockSize)
            {
                const auto* id = ids[rng.nextInt (18)];
                auto* param = p.getAPVTS().getParameter (id);
                const auto v = rng.nextFloat();
                param->setValueNotifyingHost (v);
                log << id << "=" << juce::String (param->convertFrom0to1 (v), 3) << "@" << juce::String ((double) pos / sampleRate, 1) << " ";
            }
            block.clear();
            p.processBlock (block, midi);
            finite = finite && allFinite (block);
            const auto m = juce::jmax (p.getStringLevel(), block.getMagnitude (0, 0, blockSize) > 1.996f ? 99.0f : 0.0f);
            worst = juce::jmax (worst, m);
            auto& ps = perSecond[(size_t) juce::jmin (5, pos / seconds (1.0))];
            ps = juce::jmax (ps, m);
        }
        if (! finite || worst > 3.0f)
        {
            ++bad;
            juce::String secs; for (auto v : perSecond) secs << juce::String (v, 2) << " ";
            std::cout << "BAD live run " << run << " peak " << worst << (finite ? "" : " NaN") << " per second: " << secs << std::endl;
            std::cout << "   " << log.substring (0, 1500) << std::endl;
        }
    }
    std::cout << bad << " bad of " << count << std::endl;
    return 0;
}

} // namespace chorda_test
