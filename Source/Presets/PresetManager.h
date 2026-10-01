/*
  ==============================================================================

    PresetManager.h

    Factory (compiled-in) and user (XML on disk) presets over an APVTS. The
    current name is a property of the state tree, so it is saved with the
    session. Message thread only, except isModified().

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

    /** User preset folder; created on first save. */
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

    /** Writes the current values under this name, overwriting an existing user
        preset. Fails for factory preset names. @returns true on success */
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    //==============================================================================
    /** Factory presets, then user presets; the order used for browsing. */
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

    /** Call after restoring host state to clear the modified flag. */
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
    bool applyingPreset = false;   ///< set during applyValues() so its own changes do not mark the preset modified

    static constexpr auto presetNameProperty = "presetName";
    static constexpr auto xmlRootTag         = "ChordaPreset";
    static constexpr auto xmlParamTag        = "PARAM";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace pluck
