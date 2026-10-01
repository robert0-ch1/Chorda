/*
  ==============================================================================

    PluckSynth.cpp

  ==============================================================================
*/

#include "PluckSynth.h"

namespace pluck
{

void PluckSynth::setMaxPolyphony (int polyphony) noexcept
{
    maxPolyphony = juce::jlimit (1, getNumVoices(), polyphony);
}

int PluckSynth::getActiveVoiceCount() const noexcept
{
    int active = 0;
    for (auto* voice : voices)
        if (voice->isVoiceActive())
            ++active;
    return active;
}

juce::SynthesiserVoice* PluckSynth::findFreeVoice (juce::SynthesiserSound* soundToPlay, int, int,
                                                   bool stealIfNoneAvailable) const
{
    const auto limit = juce::jmin (maxPolyphony, voices.size());

    for (int i = 0; i < limit; ++i)
    {
        auto* voice = voices.getUnchecked (i);
        if (! voice->isVoiceActive() && voice->canPlaySound (soundToPlay))
            return voice;
    }

    if (! stealIfNoneAvailable)
        return nullptr;

    // Steal the oldest releasing voice, else the oldest.
    juce::SynthesiserVoice* oldestReleasing = nullptr;
    juce::SynthesiserVoice* oldest = nullptr;

    for (int i = 0; i < limit; ++i)
    {
        auto* voice = voices.getUnchecked (i);
        if (! voice->canPlaySound (soundToPlay))
            continue;

        if (oldest == nullptr || voice->wasStartedBefore (*oldest))
            oldest = voice;

        if (auto* karplus = dynamic_cast<KarplusVoice*> (voice); karplus != nullptr && karplus->isReleasing())
            if (oldestReleasing == nullptr || voice->wasStartedBefore (*oldestReleasing))
                oldestReleasing = voice;
    }

    return oldestReleasing != nullptr ? oldestReleasing : oldest;
}

//==============================================================================
void PluckSynth::setLegato (bool shouldBeLegato) noexcept
{
    if (legato == shouldBeLegato)
        return;

    legato = shouldBeLegato;
    heldNotes.clearQuick();
    legatoStartedNote = -1;
}

KarplusVoice* PluckSynth::legatoVoice() const noexcept
{
    for (auto* v : voices)
        if (auto* voice = dynamic_cast<KarplusVoice*> (v))
            if (voice->isVoiceActive() && ! voice->isReleasing())
                return voice;

    return nullptr;
}

void PluckSynth::noteOn (int midiChannel, int midiNoteNumber, float velocity)
{
    const auto from = previousNoteHz;
    previousNoteHz  = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);

    if (legato)
    {
        heldNotes.removeAllInstancesOf (midiNoteNumber);
        heldNotes.add (midiNoteNumber);

        // Overlapping key: slide the ringing voice, no retrigger.
        if (auto* voice = legatoVoice())
        {
            voice->slideToNote (midiNoteNumber);
            return;
        }
    }

    juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
    legatoStartedNote = midiNoteNumber;

    // Portamento: start the new voice at the previous pitch.
    if (glideEnabled && from > 0.0f)
        for (auto* v : voices)
            if (auto* voice = dynamic_cast<KarplusVoice*> (v))
                if (voice->isVoiceActive() && ! voice->isReleasing()
                    && voice->getCurrentlyPlayingNote() == midiNoteNumber)
                    voice->glideFromFrequency (from);
}

void PluckSynth::noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff)
{
    if (! legato)
    {
        juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
        return;
    }

    heldNotes.removeAllInstancesOf (midiNoteNumber);

    // Return to the most recent held key instead of releasing.
    if (! heldNotes.isEmpty())
    {
        if (auto* voice = legatoVoice())
        {
            voice->slideToNote (heldNotes.getLast());
            return;
        }
    }

    if (legatoStartedNote >= 0)
    {
        juce::Synthesiser::noteOff (midiChannel, legatoStartedNote, velocity, allowTailOff);
        legatoStartedNote = -1;
    }
}

void PluckSynth::allNotesOff (int midiChannel, bool allowTailOff)
{
    heldNotes.clearQuick();
    legatoStartedNote = -1;
    juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
}

} // namespace pluck
