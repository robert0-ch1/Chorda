/*
  ==============================================================================

    StereoWidener.h

    Turns the mono string into a wide stereo image. An ensemble of four
    modulated delay taps, two per side, each side's taps 120 degrees apart in
    the modulation cycle and every left tap 180 degrees from its right twin,
    so the two channels drift against each other and never line up. A faster,
    shallower ripple on top gives the shimmer. The dry signal is faded down
    as the width comes up, so at full width the sound is almost all ensemble.

    Width 0 is bit-exact mono on both channels.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck
{

class StereoWidener
{
public:
    void prepare (double sampleRate, int maximumBlockSize)
    {
        rate = sampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maximumBlockSize, 1 };

        for (auto* line : { &left, &right })
        {
            line->prepare (spec);
            line->setMaximumDelayInSamples ((int) (0.06 * sampleRate) + 4);
            line->reset();
        }

        phase = shimmerPhase = 0.0f;
        widthSmoothed.reset (sampleRate, 0.05);
    }

    void reset()
    {
        left.reset();
        right.reset();
    }

    /** Writes left and right from mono. The three pointers may not alias. */
    void process (const float* mono, float* outLeft, float* outRight, int numSamples, float width)
    {
        widthSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, width));

        const auto baseA = (float) (0.0130 * rate);   // the two tap lengths per side
        const auto baseB = (float) (0.0094 * rate);
        const auto sweep = (float) (0.0028 * rate);   // full modulation depth at width 1
        const auto ripple = (float) (0.00018 * rate); // the fast shimmer
        const auto increment = lfoRateHz / (float) rate;
        constexpr auto twoPi = juce::MathConstants<float>::twoPi;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto w  = widthSmoothed.getNextValue();
            const auto in = mono[i];

            left.pushSample (0, in);
            right.pushSample (0, in);

            if (w <= 0.0f)
            {
                // Keep the delay lines running so a later width change is seamless
                left.popSample (0, baseA, true);
                right.popSample (0, baseA, true);
                outLeft[i] = outRight[i] = in;
            }
            else
            {
                const auto depth   = sweep * w;
                const auto shimmer = ripple * w * std::sin (twoPi * shimmerPhase);

                auto tap = [&] (juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>& line,
                                float base, float phaseOffset, bool advance)
                {
                    const auto delay = base + depth * std::sin (twoPi * (phase + phaseOffset)) + shimmer;
                    return line.popSample (0, juce::jmax (1.0f, delay), advance);
                };

                const auto wetL = 0.5f * (tap (left,  baseA, 0.0f,        false) + tap (left,  baseB, 2.0f / 3.0f, true));
                const auto wetR = 0.5f * (tap (right, baseA, 0.5f,        false) + tap (right, baseB, 1.0f / 6.0f, true));

                const auto dry  = 1.0f - 0.7f * w;
                const auto norm = 1.0f + 0.2f * w;   // the taps add incoherently: a little make-up keeps the level
                outLeft[i]  = (in * dry + wetL * w) * norm;
                outRight[i] = (in * dry + wetR * w) * norm;
            }

            phase += increment;
            if (phase >= 1.0f)
                phase -= 1.0f;

            // The shimmer runs 6.7 times faster on its own phase. It used to
            // be worked out from the sweep's phase, and since 6.7 is not a
            // whole number it jumped every time the sweep wrapped: a click
            // every 3.2 s on any held note with Width up.
            shimmerPhase += increment * shimmerRatio;
            if (shimmerPhase >= 1.0f)
                shimmerPhase -= 1.0f;
        }
    }

private:
    static constexpr float lfoRateHz = 0.31f;
    static constexpr float shimmerRatio = 6.7f;   ///< the shimmer's rate against the sweep's

    double rate = 48000.0;
    float  phase = 0.0f, shimmerPhase = 0.0f;
    juce::SmoothedValue<float> widthSmoothed;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> left, right;
};

} // namespace pluck
