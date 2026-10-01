/*
  ==============================================================================

    KarplusDamper.cpp

    What is heard of the string: the pickup comb at the pick, then the
    dampener (the blue dot), with their level make-up.
    Part of KarplusVoice; everything here runs on the audio thread.

  ==============================================================================
*/

#include "KarplusVoice.h"
#include "VoiceConstants.h"

namespace pluck
{

using namespace voice;

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

} // namespace pluck
