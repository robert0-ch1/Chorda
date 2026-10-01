/*
  ==============================================================================

    PluckSynth.h

    juce::Synthesiser with a polyphony limit, legato and portamento.
    Only the first N voices are allocated, so changing N never allocates.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "KarplusVoice.h"

namespace pluck
{

class PluckSynth final : public juce::Synthesiser
{
public:
    PluckSynth() = default;

    /** Maximum sounding voices, clamped to the voice count. */
    void setMaxPolyphony (int polyphony) noexcept;

    /** Voices currently sounding. */
    int getActiveVoiceCount() const noexcept;

    /** Mono without retrigger: overlapping keys slide the ringing string. */
    void setLegato (bool shouldBeLegato) noexcept;

    /** Portamento: new notes start at the previous note's pitch. */
    void setGlideEnabled (bool shouldGlide) noexcept   { glideEnabled = shouldGlide; }

    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void allNotesOff (int midiChannel, bool allowTailOff) override;

protected:
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* soundToPlay, int midiChannel,
                                           int midiNoteNumber, bool stealIfNoneAvailable) const override;

private:
    /** Active, non-releasing voice used by legato, or nullptr. */
    KarplusVoice* legatoVoice() const noexcept;

    int  maxPolyphony = 16;
    bool legato       = false;
    bool glideEnabled = false;

    juce::Array<int> heldNotes;     ///< held keys, oldest first
    int   legatoStartedNote = -1;   ///< note the base class assigned to the voice
    float previousNoteHz    = 0.0f; ///< glide start for the next note

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluckSynth)
};

} // namespace pluck
