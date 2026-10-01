/*
  ==============================================================================

    PresetBar.h

    Preset stepper (previous, name, next) with save, save as and delete
    buttons. The name opens the full list; "*" marks unsaved edits.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../Presets/PresetManager.h"
#include "IconButton.h"

namespace pluck::ui
{

class PresetBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit PresetBar (PresetManager& manager);
    ~PresetBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void refreshDisplayedName();

    void showPresetList();
    void saveClicked();
    void saveAsClicked();
    void deleteClicked();

    void askForName (const juce::String& initialName, std::function<void (juce::String)> onAccept);
    void confirm (const juce::String& title, const juce::String& message,
                  const juce::String& okText, std::function<void()> onConfirm);
    void showError (const juce::String& message);

    PresetManager& presets;

    juce::TextButton previousButton { "<" }, nextButton { ">" };
    juce::TextButton nameButton;
    IconButton saveButton   { "Save",    IconButton::Icon::save };
    IconButton saveAsButton { "Save As", IconButton::Icon::saveAs };
    IconButton deleteButton { "Delete",  IconButton::Icon::trash };

    juce::Rectangle<int> stepper;
    juce::String shownCurrent;
    bool         shownModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace pluck::ui
