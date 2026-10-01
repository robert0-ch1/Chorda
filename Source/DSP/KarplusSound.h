/*
  ==============================================================================

    KarplusSound.h

    Single SynthesiserSound accepting all notes and channels, matched by
    KarplusVoice::canPlaySound().

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
