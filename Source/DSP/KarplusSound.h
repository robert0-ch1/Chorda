/*
  ==============================================================================

    KarplusSound.h

    juce::Synthesiser pairs every voice with a "sound" object that decides
    which notes and channels it responds to. Chorda has a single
    string model that covers the whole keyboard, so this class is trivially
    permissive. It exists only so KarplusVoice::canPlaySound() has something
    to match against.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck
{

class KarplusSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override      { return true; }
    bool appliesToChannel (int) override   { return true; }
};

} // namespace pluck
