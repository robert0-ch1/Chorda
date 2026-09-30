/*
  ==============================================================================

    PluckSynth.h

    juce::Synthesiser with a polyphony limit, a legato mode and portamento.

    Only the first N voices are ever handed out, so changing the limit is a
    single store, no allocation on the audio thread. N = 1 is mono: every
    note re-plucks the single string. When all N voices are busy the one
    already being released is stolen first, then the oldest.

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

    /** Sets how many voices may sound. */
    void setMaxPolyphony (int polyphony) noexcept;

    /** Voices currently sounding. */
    int getActiveVoiceCount() const noexcept;

    /** Legato: one string, and a new key while another is down slides it
        rather than plucking it again. */
    void setLegato (bool shouldBeLegato) noexcept;

    /** Whether a new note starts at the pitch of the one before it. */
    void setGlideEnabled (bool shouldGlide) noexcept   { glideEnabled = shouldGlide; }

    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void allNotesOff (int midiChannel, bool allowTailOff) override;

protected:
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound* soundToPlay, int midiChannel,
                                           int midiNoteNumber, bool stealIfNoneAvailable) const override;

private:
    /** The one voice a legato phrase is riding on, if any. */
    KarplusVoice* legatoVoice() const noexcept;

    int  maxPolyphony = 16;
    bool legato       = false;
    bool glideEnabled = false;

    juce::Array<int> heldNotes;     ///< keys down, in the order they went down
    int   legatoStartedNote = -1;   ///< the note the synthesiser thinks that voice holds
    float previousNoteHz    = 0.0f; ///< where the next note glides from

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluckSynth)
};

} // namespace pluck
