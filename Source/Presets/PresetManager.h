/*
  ==============================================================================

    PresetManager.h

    Loads, saves and browses presets on top of an AudioProcessorValueTreeState.

    Two kinds of preset exist:
      * factory presets, compiled into the binary (see FactoryPresets.cpp);
      * user presets, XML files in a per-user folder.

    The name of the current preset is stored as a property on the APVTS state
    tree, so it travels with the host session. A "modified" flag is raised
    whenever any parameter changes after a load or save, and the UI shows it
    as an asterisk.

    Everything here is meant to be called from the message thread, except
    isModified(), which is safe from anywhere.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <map>

namespace pluck
{

class PresetManager final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state);
    ~PresetManager() override;

    //==============================================================================
    /** File extension for user presets, including the dot. */
    static constexpr auto fileExtension = ".chordapreset";

    /** Where user presets live. Created on first save. */
    static juce::File getUserPresetDirectory();

    //==============================================================================
    int          getNumFactoryPresets() const;
    juce::String getFactoryPresetName (int index) const;
    void         loadFactoryPreset (int index);

    //==============================================================================
    /** Names of the user presets on disk, sorted alphabetically. */
    juce::StringArray getUserPresetNames() const;

    bool userPresetExists (const juce::String& name) const;
    bool isFactoryPresetName (const juce::String& name) const;

    /** Saves the current parameter values under this name, overwriting any
        existing user preset. Refuses names that clash with a factory preset.
        @returns true on success */
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    //==============================================================================
    /** Factory presets first, then user presets. Used for prev/next browsing. */
    juce::StringArray getAllPresetNames() const;

    /** Index of the current preset in getAllPresetNames(), or -1. */
    int  getCurrentPresetIndex() const;
    void loadPresetAtIndex (int index);
    void loadNextPreset();
    void loadPreviousPreset();

    //==============================================================================
    juce::String getCurrentPresetName() const;

    /** True if any parameter changed since the last load or save. Thread-safe. */
    bool isModified() const noexcept   { return modified.load(); }

    /** Call after the processor restores host state, so the flag reflects the
        freshly loaded session rather than the edits that preceded it. */
    void stateRestored();

private:
    //==============================================================================
    using ValueMap = std::map<juce::String, float>;

    void applyValues (const ValueMap& values, const juce::String& presetName);
    void setCurrentPresetName (const juce::String& name);
    static juce::File fileForPreset (const juce::String& name);
    static juce::String sanitiseName (const juce::String& name);

    void parameterChanged (const juce::String& parameterID, float newValue) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<bool> modified { false };
    bool applyingPreset = false;   ///< suppresses the modified flag while a preset is being applied

    static constexpr auto presetNameProperty = "presetName";
    static constexpr auto xmlRootTag         = "ChordaPreset";
    static constexpr auto xmlParamTag        = "PARAM";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace pluck
