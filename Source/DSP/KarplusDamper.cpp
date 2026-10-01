/*
  ==============================================================================

    KarplusDamper.cpp

    Output filtering: pickup comb at the pick position, then the damper comb
    cascade, with level make-up. Audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

float KarplusVoice::currentDamperPosition() const noexcept
{
    // The LFO modulates position only while the damper is on.
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
    // p and 1 - p share nodes, so work on the mirrored position. 0 is off.
    position = mirroredPosition (position);
    if (position <= 0.0f)
        return 0.0f;

    // Fractional, so moving notches slide smoothly.
    const auto maxDelay = ((float) damperHistory.size() - 4.0f) / (float) damperStages;
    return juce::jlimit (1.0f, maxDelay, position * periodSamples);
}

void KarplusVoice::updateDamperFilter() noexcept
{
    // Off fades the depth to zero and keeps the last delay.
    const auto delay = damperDelayFor (currentDamperPosition());
    damperMixTarget = delay > 0.0f ? maximumDamperMix * currentDamperPressure() : 0.0f;
    if (delay > 0.0f)
        damperDelayTarget = delay;

    // Switching on: jump to the delay, fade the depth in.
    if (damperDelay <= 0.0f)
        damperDelay = damperDelayTarget;

    // Pickup at the pick position.
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

    // Binomial tap weights of ((1 - m) + m z^-D)^N.
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

    auto index = writeIndex - 1 - whole;   // newest sample is just behind the write index
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
    // Pickup at the pick point q: mode n is weighted by sin (n pi q), i.e. the
    // comb (1 - z^-(qP)) / 2. Outside the loop, so it follows pick changes on a
    // ringing note without feeding back.
    listenHistory[(size_t) listenWriteIndex] = input;
    if (++listenWriteIndex >= (int) listenHistory.size())
        listenWriteIndex = 0;

    listenDelay += (listenDelayTarget - listenDelay) * damperGlideCoeff;
    const auto heard = 0.5f * (input - readHistory (listenHistory, listenWriteIndex, listenDelay));

    // History always runs so the damper has valid input when it switches on.
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

    // Level make-up against the unfiltered string, delayed by the filters'
    // group delay (half the pickup delay plus N m D) so attacks line up.
    // Both mean squares share one window, so the gain is steady over the envelope.
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

} // namespace pluck
