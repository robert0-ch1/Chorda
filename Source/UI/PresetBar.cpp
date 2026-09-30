/*
  ==============================================================================

    PresetBar.cpp

  ==============================================================================
*/

#include "PresetBar.h"
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

PresetBar::PresetBar (PresetManager& manager)
    : presets (manager)
{
    for (auto* button : { &previousButton, &nextButton, &nameButton })
        button->setWantsKeyboardFocus (false);

    // The stepper draws its own frame in paint(); the icon buttons draw theirs.
    for (auto* button : std::initializer_list<juce::Button*> { &previousButton, &nextButton, &nameButton,
                                                              &saveButton, &saveAsButton, &deleteButton })
        addAndMakeVisible (button);

    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    nameButton.setTooltip ("Choose a preset");
    saveButton.setTooltip ("Save: overwrite the current user preset, or name a new one when a factory preset is loaded");
    saveAsButton.setTooltip ("Save as: store the current settings as a new user preset");
    deleteButton.setTooltip ("Delete the current user preset");

    previousButton.onClick = [this] { presets.loadPreviousPreset(); refreshDisplayedName(); };
    nextButton.onClick     = [this] { presets.loadNextPreset();     refreshDisplayedName(); };
    nameButton.onClick     = [this] { showPresetList(); };
    saveButton.onClick     = [this] { saveClicked(); };
    saveAsButton.onClick   = [this] { saveAsClicked(); };
    deleteButton.onClick   = [this] { deleteClicked(); };

    refreshDisplayedName();
    startTimerHz (10);
}

PresetBar::~PresetBar()
{
    stopTimer();
}

void PresetBar::resized()
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;   // not laid out yet (the constructor refreshes the name before we have a size)

    const auto h = bounds.getHeight();

    // Three square icon buttons on the right
    deleteButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (6);
    saveAsButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (6);
    saveButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (14);

    // Stepper: [<][ name ][>] as one field, filling what is left
    stepper = bounds;
    auto inner = stepper;
    previousButton.setBounds (inner.removeFromLeft (h));
    nextButton.setBounds (inner.removeFromRight (h));
    nameButton.setBounds (inner);
}

void PresetBar::paint (juce::Graphics& g)
{
    // The stepper's shared frame and the two separators inside it
    const auto frame = stepper.toFloat().reduced (0.5f);
    g.setColour (colours::field);
    g.fillRoundedRectangle (frame, 4.0f);
    g.setColour (colours::hairline);
    g.drawRoundedRectangle (frame, 4.0f, 1.0f);
    g.drawVerticalLine (previousButton.getRight(), frame.getY() + 1.0f, frame.getBottom() - 1.0f);
    g.drawVerticalLine (nextButton.getX(),         frame.getY() + 1.0f, frame.getBottom() - 1.0f);
}

//==============================================================================
void PresetBar::timerCallback()
{
    refreshDisplayedName();
}

void PresetBar::refreshDisplayedName()
{
    const auto name     = presets.getCurrentPresetName();
    const auto modified = presets.isModified();

    if (name == shownCurrent && modified == shownModified)
        return;

    shownCurrent  = name;
    shownModified = modified;

    nameButton.setButtonText (modified ? name + " *" : name);
    // Save is always there: on a user preset it overwrites, and on a factory
    // preset, which is built in and cannot be overwritten, it asks for a name.
    const bool isUserPreset = ! presets.isFactoryPresetName (name) && presets.userPresetExists (name);
    deleteButton.setEnabled (isUserPreset);
    resized();
}

//==============================================================================
void PresetBar::showPresetList()
{
    juce::PopupMenu menu;
    const auto names = presets.getAllPresetNames();
    const auto numFactory = presets.getNumFactoryPresets();
    const auto current = presets.getCurrentPresetIndex();

    // Item IDs are 1-based indices into getAllPresetNames().
    menu.addSectionHeader ("Factory");
    for (int i = 0; i < numFactory; ++i)
        menu.addItem (i + 1, names[i], true, i == current);

    if (names.size() > numFactory)
    {
        menu.addSeparator();
        menu.addSectionHeader ("User");
        for (int i = numFactory; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton).withMinimumWidth (nameButton.getWidth()),
                        [this] (int id)
                        {
                            if (id > 0)
                            {
                                presets.loadPresetAtIndex (id - 1);
                                refreshDisplayedName();
                            }
                        });
}

void PresetBar::saveClicked()
{
    const auto current = presets.getCurrentPresetName();

    if (presets.isFactoryPresetName (current) || ! presets.userPresetExists (current))
    {
        saveAsClicked();   // nothing to overwrite
        return;
    }

    if (! presets.saveUserPreset (current))
        showError ("Could not write the preset file. Check that the presets folder is writable:\n"
                   + PresetManager::getUserPresetDirectory().getFullPathName());
    refreshDisplayedName();
}

void PresetBar::saveAsClicked()
{
    const auto current = presets.getCurrentPresetName();
    const auto suggestion = presets.isFactoryPresetName (current) ? current + " 2" : current;

    askForName (suggestion, [this] (juce::String name)
    {
        name = name.trim();
        if (name.isEmpty())
            return;

        if (presets.isFactoryPresetName (name))
        {
            showError ("\"" + name + "\" is a factory preset name. Please choose another.");
            return;
        }

        auto doSave = [this, name]
        {
            if (! presets.saveUserPreset (name))
                showError ("Could not write the preset file. Check that the presets folder is writable:\n"
                           + PresetManager::getUserPresetDirectory().getFullPathName());
            refreshDisplayedName();
        };

        if (presets.userPresetExists (name))
            confirm ("Overwrite preset?", "A preset called \"" + name + "\" already exists.", "Overwrite", doSave);
        else
            doSave();
    });
}

void PresetBar::deleteClicked()
{
    const auto name = presets.getCurrentPresetName();

    if (presets.isFactoryPresetName (name) || ! presets.userPresetExists (name))
        return;

    confirm ("Delete preset?", "\"" + name + "\" will be removed from disk. This cannot be undone.", "Delete",
             [this, name]
             {
                 presets.deleteUserPreset (name);
                 refreshDisplayedName();
             });
}

//==============================================================================
// Plugins must not run modal loops, so every dialog below is asynchronous.

void PresetBar::askForName (const juce::String& initialName, std::function<void (juce::String)> onAccept)
{
    auto* window = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", initialName, {});
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, onAccept] (int result)
    {
        if (result == 1 && safeWindow != nullptr)
            onAccept (safeWindow->getTextEditorContents ("name"));
    }), true);
}

void PresetBar::confirm (const juce::String& title, const juce::String& message,
                         const juce::String& okText, std::function<void()> onConfirm)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, message, okText, "Cancel", this,
                                        juce::ModalCallbackFunction::create ([onConfirm] (int result)
                                        {
                                            if (result == 1)
                                                onConfirm();
                                        }));
}

void PresetBar::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Presets", message, "OK", this);
}

} // namespace pluck::ui
