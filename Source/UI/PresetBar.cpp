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

    // The stepper frame is drawn in paint().
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
        return;   // called from the constructor before the first layout

    const auto h = bounds.getHeight();

    // Square icon buttons on the right
    deleteButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (6);
    saveAsButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (6);
    saveButton.setBounds (bounds.removeFromRight (h));
    bounds.removeFromRight (14);

    // Stepper [<][name][>] fills the remaining width
    stepper = bounds;
    auto inner = stepper;
    previousButton.setBounds (inner.removeFromLeft (h));
    nextButton.setBounds (inner.removeFromRight (h));
    nameButton.setBounds (inner);
}

void PresetBar::paint (juce::Graphics& g)
{
    // Stepper frame and separators
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
    // Save stays enabled: on a factory preset it falls back to save as.
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

    menu.setLookAndFeel (&getLookAndFeel());
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
        saveAsClicked();
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
// Dialogs are asynchronous: plugins must not run modal loops.

juce::AlertWindow* PresetBar::makeDialog (const juce::String& title, const juce::String& message)
{
    // The look-and-feel is set on each window rather than relied on as the
    // global default, which another instance's editor may reset.
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&getLookAndFeel());
    return window;
}

void PresetBar::addDialogButtons (juce::AlertWindow& window, const juce::String& okText, bool withCancel)
{
    window.addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (withCancel)
        window.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window.getButton (0)->getProperties().set (PluckLookAndFeel::primaryProperty, true);
    if (withCancel)
        window.getButton (1)->getProperties().set (PluckLookAndFeel::borderedProperty, true);
}

void PresetBar::askForName (const juce::String& initialName, std::function<void (juce::String)> onAccept)
{
    auto* window = makeDialog ("Save preset", "Name");
    window->addTextEditor ("name", initialName, {});
    addDialogButtons (*window, "Save", true);

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
    auto* window = makeDialog (title, message);
    addDialogButtons (*window, okText, true);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([onConfirm] (int result)
    {
        if (result == 1)
            onConfirm();
    }), true);
}

void PresetBar::showError (const juce::String& message)
{
    auto* window = makeDialog ("Presets", message);
    addDialogButtons (*window, "OK", false);
    window->enterModalState (true, nullptr, true);
}

} // namespace pluck::ui
