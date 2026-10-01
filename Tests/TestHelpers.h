/*
  ==============================================================================

    TestHelpers.h

    Shared by the test files: rendering MIDI through the processor, setting
    parameters, and the measurements the tests make on the audio.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace chorda_test
{

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

struct MidiEvent
{
    int sample;
    juce::MidiMessage message;
};

constexpr double sampleRate = 48000.0;

constexpr int    blockSize  = 256;

inline int seconds (double s)   { return juce::roundToInt (s * sampleRate); }

/** Runs the processor block by block with the given events and returns the stereo output. */
inline juce::AudioBuffer<float> render (ChordaAudioProcessor& processor, std::vector<MidiEvent> events,
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

inline void setParameter (ChordaAudioProcessor& processor, const char* id, float value)
{
    auto* param = processor.getAPVTS().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

inline float getParameter (ChordaAudioProcessor& processor, const char* id)
{
    return processor.getAPVTS().getRawParameterValue (id)->load();
}

/** Disables everything that would blur a measurement: reverb, LFO, drive, width.
    Uses the sine exciter so renders are deterministic (the noise exciter is random). */
inline void makeDryTestPatch (ChordaAudioProcessor& processor)
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

inline float peakBetween (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    start = juce::jlimit (0, buffer.getNumSamples(), start);
    end   = juce::jlimit (start, buffer.getNumSamples(), end);
    return end > start ? buffer.getMagnitude (0, start, end - start) : 0.0f;
}

/** Largest sample-to-sample jump on the left channel in [start, end). A click is a big jump. */
inline float maxStepBetween (const juce::AudioBuffer<float>& buffer, int start, int end)
{
    const auto* data = buffer.getReadPointer (0);
    start = juce::jmax (1, start);
    end   = juce::jmin (buffer.getNumSamples(), end);

    float maxStep = 0.0f;
    for (int i = start; i < end; ++i)
        maxStep = juce::jmax (maxStep, std::abs (data[i] - data[i - 1]));

    return maxStep;
}

inline bool allFinite (const juce::AudioBuffer<float>& buffer)
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

/** Mean square of the left channel after BS.1770 K-weighting (48 kHz coefficients).
    A loudness measure, so bright and dark plucks compare as heard. */
inline double kWeightedMeanSquare (const juce::AudioBuffer<float>& audio, int start, int length)
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

inline void writeWav (const juce::File& directory, const juce::String& name, const juce::AudioBuffer<float>& buffer)
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

/** Estimates the fundamental of a buffer region: zero-padded FFT of a Hann-windowed
    segment, peak search around the expected frequency, parabolic interpolation
    on the log magnitude. Accurate to well under a cent for a steady tone. */
inline float estimateFrequency (const juce::AudioBuffer<float>& audio, int start, int length, float expectedHz)
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

/** Level of the k-th partial relative to the fundamental, in dB, from an FFT of a region. */
inline float partialLevelDb (const juce::AudioBuffer<float>& audio, int start, int length, float f0, int k)
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

/** Counts isolated clicks: points where the third difference stands `ratio` times
    above its mean over the surrounding 40 ms. */
inline int countClicks (const juce::AudioBuffer<float>& audio, int from, int to, float ratio = 20.0f)
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

/** Sign changes of a sequence around its own middle: two per cycle. */
inline int crossings (const std::vector<float>& values)
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

} // namespace chorda_test
