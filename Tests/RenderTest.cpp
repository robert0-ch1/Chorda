/*
  ==============================================================================

    RenderTest.cpp

    Offline test harness for the Chorda processor. It instantiates the
    plugin without a host, feeds it MIDI, and inspects the rendered audio:

      1. note-off produces a smooth release, not a click, and sustain /
         decay behave as string damping;
      2. releasing one note does not silence the others;
      3. hammering more notes than there are voices neither crashes nor
         produces NaNs;
      4. every factory preset renders finite audio;
      5. host state survives a save / restore round trip;
      6. a sample-rate change is handled.

    Usage:  PluckRenderTest [output-directory]
    If a directory is given, the rendered scenarios are written there as WAV
    files so you can listen to them.

            PluckRenderTest --screenshot <file.png> [pluck frame 0..2]
    Renders the editor offscreen at its default size and saves it as a PNG,
    optionally with the pick hand held on one frame of its pluck.
    Used to keep the README screenshot in sync with the real UI.

    Exit code is the number of failed checks (0 = all good).

  ==============================================================================
*/

#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{

//==============================================================================
struct TestReport
{
    int passed = 0, failed = 0;

    void check (bool condition, const juce::String& description, const juce::String& detail = {})
    {
        const auto line = juce::String (condition ? "  PASS  " : "  FAIL  ") + description
                        + (detail.isNotEmpty() ? "  [" + detail + "]" : juce::String());
        std::cout << line << std::endl;
        (condition ? passed : failed)++;
    }

    void section (const juce::String& title)
    {
        std::cout << "\n" << title << std::endl;
    }
};

//==============================================================================
struct MidiEvent
{
    int sample;
    juce::MidiMessage message;
};

constexpr double sampleRate = 48000.0;
constexpr int    blockSize  = 256;

int seconds (double s)   { return juce::roundToInt (s * sampleRate); }

/** Runs the processor block by block with the given events and returns the stereo output. */
juce::AudioBuffer<float> render (ChordaAudioProcessor& processor, std::vector<MidiEvent> events,
                                 int totalSamples, double rate = sampleRate)
{
    std::sort (events.begin(), events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.sample < b.sample; });

    processor.setPlayConfigDetails (0, 2, rate, blockSize);
    processor.prepareToPlay (rate, blockSize);

    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    juce::AudioBuffer<float> block (2, blockSize);
    size_t nextEvent = 0;

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

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, position, block, ch, 0, numSamples);
    }

    processor.releaseResources();
    return output;
}

//==============================================================================
void setParameter (ChordaAudioProcessor& processor, const char* id, float value)
{
    auto* param = processor.getAPVTS().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float getParameter (ChordaAudioProcessor& processor, const char* id)
{
    return processor.getAPVTS().getRawParameterValue (id)->load();
}

/** Disables everything that would blur a measurement: reverb, LFO, drive, width.
    Uses the sine exciter so renders are deterministic (the noise exciter is random). */
void makeDryTestPatch (ChordaAudioProcessor& processor)
{
    using namespace pluck::ParamID;
    setParameter (processor, exciterTone,     pluck::toneSine);
    setParameter (processor, exciterAttack,   10.0f);
    setParameter (processor, exciterPosition, 0.2f);
    setParameter (processor, stringSub,       0.0f);
    setParameter (processor, stringDamperPressure, 0.6f);
    setParameter (processor, pitchOctave,     (float) pluck::octaveDefaultIndex);
    setParameter (processor, outputDrive,     0.0f);
    setParameter (processor, outputWidth,     0.0f);
    setParameter (processor, stringDamper,    0.0f);
    setParameter (processor, voiceMode,       (float) pluck::voiceModeNames.indexOf ("16"));
    setParameter (processor, outputGain,      0.0f);
    setParameter (processor, outputReverb,    0.0f);
    setParameter (processor, lfoAmount,       0.0f);
    setParameter (processor, stringDecay,     20.0f);   // string stays loud for the whole test
    setParameter (processor, stringSustain,   0.0f);
    setParameter (processor, stringRelease,   0.1f);
}

//==============================================================================
float peakBetween (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    start = juce::jlimit (0, buffer.getNumSamples(), start);
    end   = juce::jlimit (start, buffer.getNumSamples(), end);
    return end > start ? buffer.getMagnitude (0, start, end - start) : 0.0f;
}

/** Largest sample-to-sample jump on the left channel in [start, end). A click is a big jump. */
float maxStepBetween (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    const auto* data = buffer.getReadPointer (0);
    start = juce::jmax (1, start);
    end   = juce::jmin (buffer.getNumSamples(), end);

    float maxStep = 0.0f;
    for (int i = start; i < end; ++i)
        maxStep = juce::jmax (maxStep, std::abs (data[i] - data[i - 1]));

    return maxStep;
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }
    return true;
}

/** Mean square of the left channel through the BS.1770 K-weighting filter
    (48 kHz coefficients): a loudness measure rather than a plain level, so a
    bright noise burst and a dark tone are compared the way the ear hears them. */
double kWeightedMeanSquare (const juce::AudioBuffer<float>& audio, int start, int length)
{
    const double b1[] { 1.53512485958697, -2.69169618940638, 1.19839281085285 }, a1[] { -1.69065929318241, 0.73248077421585 };
    const double b2[] { 1.0, -2.0, 1.0 },                                         a2[] { -1.99004745483398, 0.99007225036621 };
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0, sum = 0;
    const auto* data = audio.getReadPointer (0);
    const auto end = juce::jmin (audio.getNumSamples(), start + length);

    for (int i = start; i < end; ++i)
    {
        const double x = data[i];
        const double y = b1[0] * x + b1[1] * x1 + b1[2] * x2 - a1[0] * y1 - a1[1] * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        const double v = b2[0] * y + b2[1] * u1 + b2[2] * u2 - a2[0] * v1 - a2[1] * v2;
        u2 = u1; u1 = y; v2 = v1; v1 = v;
        sum += v * v;
    }
    return sum / juce::jmax (1, end - start);
}

void writeWav (const juce::File& directory, const juce::String& name, const juce::AudioBuffer<float>& buffer)
{
    if (directory == juce::File())
        return;

    directory.createDirectory();
    const auto file = directory.getChildFile (name + ".wav");
    file.deleteFile();

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (file),
                                                                             sampleRate, 2, 24, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

//==============================================================================
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

/** Estimates the fundamental of a buffer region: zero-padded FFT of a Hann-windowed
    segment, peak search around the expected frequency, parabolic interpolation
    on the log magnitude. Accurate to well under a cent for a steady tone. */
float estimateFrequency (const juce::AudioBuffer<float>& audio, int start, int length, float expectedHz)
{
    constexpr int order = 20;                 // 1M points: ~0.05 Hz bins at 48 kHz
    constexpr int fftSize = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);

    const auto* samples = audio.getReadPointer (0);
    const auto n = juce::jmin (length, fftSize);
    for (int i = 0; i < n; ++i)
    {
        const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
        data[(size_t) i] = samples[start + i] * window;
    }

    fft.performFrequencyOnlyForwardTransform (data.data());

    const auto binHz = (float) sampleRate / (float) fftSize;
    const auto lo = juce::jmax (1, (int) (expectedHz * 0.94f / binHz));
    const auto hi = juce::jmin (fftSize / 2 - 2, (int) (expectedHz * 1.06f / binHz));

    int peak = lo;
    for (int k = lo; k <= hi; ++k)
        if (data[(size_t) k] > data[(size_t) peak])
            peak = k;

    const auto a = std::log (juce::jmax (data[(size_t) peak - 1], 1.0e-12f));
    const auto b = std::log (juce::jmax (data[(size_t) peak],     1.0e-12f));
    const auto c = std::log (juce::jmax (data[(size_t) peak + 1], 1.0e-12f));
    const auto denom = a - 2.0f * b + c;
    const auto offset = std::abs (denom) > 1.0e-12f ? 0.5f * (a - c) / denom : 0.0f;
    return ((float) peak + offset) * binHz;
}

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

/** Level of the k-th partial relative to the fundamental, in dB, from an FFT of a region. */
float partialLevelDb (const juce::AudioBuffer<float>& audio, int start, int length, float f0, int k)
{
    constexpr int order = 16;
    constexpr int fftSize = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) fftSize * 2, 0.0f);

    const auto* samples = audio.getReadPointer (0);
    const auto n = juce::jmin (length, fftSize);
    for (int i = 0; i < n; ++i)
        data[(size_t) i] = samples[start + i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1)));
    fft.performFrequencyOnlyForwardTransform (data.data());

    auto peakAround = [&] (float hz)
    {
        const auto binHz = (float) sampleRate / (float) fftSize;
        const auto lo = juce::jmax (1, (int) (hz * 0.97f / binHz)), hi = juce::jmin (fftSize / 2 - 1, (int) (hz * 1.03f / binHz));
        float best = 0.0f;
        for (int b = lo; b <= hi; ++b) best = juce::jmax (best, data[(size_t) b]);
        return juce::jmax (best, 1.0e-9f);
    };

    return juce::Decibels::gainToDecibels (peakAround (f0 * (float) k) / peakAround (f0));
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
        // The 4th partial: the test patch picks at 20 %, a node of the 5th,
        // and the string is heard there too, so the 5th is not there to measure.
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

void testPolyphonyIsIndependent (TestReport& report, const juce::File& outDir)
{
    report.section ("2. Releasing one note keeps the others sounding");

    ChordaAudioProcessor processor;
    makeDryTestPatch (processor);

    // Reference: E on its own, so we know how loud it should be at 0.7-0.9 s.
    const auto solo = render (processor,
                              { { seconds (0.1), juce::MidiMessage::noteOn  (1, 64, 0.9f) },
                                { seconds (1.0), juce::MidiMessage::noteOff (1, 64) } },
                              seconds (1.5));

    // Same again with A held underneath and released at 0.5 s. If note-off
    // wrongly silenced every voice (the original bug), E would vanish too.
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

    // Moving a knob must flip the modified flag.
    setParameter (processor, pluck::ParamID::stringDecay, 1.234f);
    report.check (presets.isModified(), "editing a parameter marks the preset modified");
}

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

//==============================================================================
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

//==============================================================================
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

//==============================================================================
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

//==============================================================================
/** Glide slides the string from the note before this one; Legato leaves it
    ringing and only changes where it is tuned. */
void testGlideAndLegato (TestReport& report, const juce::File& outDir)
{
    using namespace pluck::ParamID;
    report.section ("1m. Glide and legato");

    const auto lowHz  = (float) juce::MidiMessage::getMidiNoteInHertz (48);
    const auto highHz = (float) juce::MidiMessage::getMidiNoteInHertz (60);

    // --- Glide: a mono voice re-plucked on a new note starts at the old pitch
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

        // Early in a long glide the string is still near where it came from;
        // a short window, because the pitch is moving under the measurement.
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

    // --- Legato: a second key while the first is down must not pluck again
    auto secondNoteAttack = [&] (const char* mode)
    {
        ChordaAudioProcessor processor;
        makeDryTestPatch (processor);
        setParameter (processor, voiceMode,  (float) pluck::voiceModeNames.indexOf (mode));
        setParameter (processor, pitchGlide, 0.001f);
        // A decay short enough that the string is well down by the time the
        // second key goes down, so plucking it again is unmistakable, but long
        // enough that it is still ringing: legato has nothing to slide if the
        // string has already died and the voice has been handed back.
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

//==============================================================================
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

//==============================================================================
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

//==============================================================================
/** Counts isolated clicks: places where the third difference of the output
    (which a smooth tone keeps small and a click makes huge) stands far above
    its own level over the surrounding 40 ms. */
int countClicks (const juce::AudioBuffer<float>& audio, int from, int to, float ratio = 20.0f)
{
    const auto* x = audio.getReadPointer (0);
    std::vector<float> d3 ((size_t) audio.getNumSamples(), 0.0f);
    for (int i = 3; i < audio.getNumSamples(); ++i)
        d3[(size_t) i] = std::abs (x[i] - 3.0f * x[i - 1] + 3.0f * x[i - 2] - x[i - 3]);

    const int w = seconds (0.02);
    int clicks = 0;
    for (int i = juce::jmax (from, w); i + w < juce::jmin (to, audio.getNumSamples()); i += 16)
    {
        double mean = 0.0;
        for (int k = i - w; k < i + w; k += 4)
            mean += d3[(size_t) k];
        mean /= (double) (2 * w / 4);

        float peak = 0.0f;
        for (int k = i; k < i + 16; ++k)
            peak = juce::jmax (peak, d3[(size_t) k]);

        if (peak > mean * ratio && peak > 1.0e-5f)
        {
            ++clicks;
            i += seconds (0.005);
        }
    }
    return clicks;
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

/** A glide plucks the string at the note it starts from, as loud as a plain
    pluck of that note. It used to set the pluck up for the note it was
    heading to: up to 4 dB too loud gliding down, 6 dB too quiet gliding up. */
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
        // Within 1.5 dB: a fast glide up leaves a bump of about 1 dB in its
        // first 40 ms, from the pickup's delay moving with the pitch. The bug
        // this guards against was 4 to 6 dB.
        report.check (std::abs (db) < 1.5f, "a glide from " + juce::String (from) + " to " + juce::String (to) + " plucks as loud as " + juce::String (from),
                      juce::String (db, 2) + " dB");
    }
}

/** Nothing runs away and nothing leaves louder than +6 dBFS. */
void testSafety (TestReport& report)
{
    using namespace pluck::ParamID;
    report.section ("9. Safety");

    // The limiter: a loud chord through full drive and +12 dB of gain.
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

    // A normal level passes the limiter untouched: width 0 stays exact mono,
    // and a quiet note is the same with the limiter's ceiling far away.
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

    // Playing and turning knobs at random, as a player might: a hundred runs
    // of six seconds, a random knob every 100 ms, random bends, a second note
    // now and then. The string itself must never pass 3 (a working one stays
    // under 1), whatever the limiter does after it.
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

/** Sign changes of a sequence around its own middle: two per cycle. */
int crossings (const std::vector<float>& values)
{
    if (values.size() < 2)
        return 0;

    auto lo = values[0], hi = values[0];
    for (auto v : values) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
    const auto middle = 0.5f * (lo + hi);

    int count = 0;
    for (size_t i = 1; i < values.size(); ++i)
        if ((values[i - 1] < middle) != (values[i] < middle))
            ++count;
    return count;
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

    // Rate and Amount: a 2 Hz sine for 3 s is six cycles, twelve crossings,
    // and 20 % Amount swings the damper a tenth of the string either side of
    // where it is set (20 %), so from 10 % to 30 %. (100 % reaches the whole string.)
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

    // The pressure LFO: bipolar around the set pressure (80 %), 20 % Amount
    // is a tenth of the range either way, at its own rate.
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

    // Following the host (120 BPM when there is none): 1.7 Hz is nearest a
    // quarter note, which at that tempo is 2 Hz.
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
/** The reverb, last in the chain. */
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

    // A send leaves the dry string alone: the first milliseconds, before the
    // room has answered, are the dry signal exactly.
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

//==============================================================================
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

//==============================================================================
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

int saveEditorScreenshot (const juce::File& file, int pluckFrame = -1)
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



//==============================================================================
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








} // namespace

//==============================================================================

//==============================================================================
/** Changing notes in Mono and Legato does not click. Mono steals its one
    voice for every note, and that used to stop the old string dead; Legato
    with no Glide retuned the loop in less than a period. */
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

        // The biggest sample-to-sample step across the change, against the
        // string's own before it. A string stopped dead was 14 to 19 dB up.
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


//==============================================================================
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

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 2 && juce::String (argv[1]) == "--fuzz-live")
        return fuzzLive (juce::String (argv[2]).getIntValue());

    if (argc > 2 && juce::String (argv[1]) == "--fuzz")
        return fuzz (juce::String (argv[2]).getIntValue());

    if (argc > 1 && juce::String (argv[1]) == "--measure-tone")
        return measureTone();

    if (argc > 2 && juce::String (argv[1]) == "--screenshot")
        return saveEditorScreenshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                                     argc > 3 ? juce::String (argv[3]).getIntValue() : -1);

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();

    std::cout << "Chorda render test" << std::endl;
    if (outDir != juce::File())
        std::cout << "Writing WAV files to " << outDir.getFullPathName() << std::endl;

    TestReport report;
    testReleaseHasNoClick (report, outDir);
    testReleaseTime (report, outDir);
    testHoldAndStability (report, outDir);
    testTuning (report);
    testStringControls (report, outDir);
    testPerformanceControls (report, outDir);
    testVoiceModes (report);
    testSustainAndDecay (report, outDir);
    testLowNotesSurviveShortPlucks (report, outDir);
    testPolyphonyIsIndependent (report, outDir);
    testVoiceStealing (report, outDir);
    testFactoryPresets (report, outDir);
    testStateRoundTrip (report);
    testSampleRateChange (report);
    testStereoWidth (report);
    testHeldNoteStaysClean (report, outDir);
    testSubTracksTheString (report, outDir);
    testPickIsAlwaysOnTheString (report);
    testToneCrossfade (report, outDir);
    testGlideAndLegato (report, outDir);
    testDamperKeepsTheEnvelope (report, outDir);
    testLowNotesLoseTheirTop (report, outDir);
    testHeldNotesDoNotClick (report, outDir);
    testNoteChangesDoNotClick (report, outDir);
    testCascadeEnergy (report);
    testDamperDragIsSmooth (report, outDir);
    testTheWholeString (report, outDir);
    testGlideLevel (report);
    testNoLatency (report);
    testBendIsClean (report);
    testPickWhileRinging (report);
    testSafety (report);
    testLfo (report, outDir);
    testReverb (report, outDir);
    testExciterLevels (report);

    std::cout << "\n" << report.passed << " passed, " << report.failed << " failed" << std::endl;
    return report.failed;
}
