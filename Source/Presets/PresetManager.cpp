/*
  ==============================================================================

    PresetManager.cpp

  ==============================================================================
*/

#include "PresetManager.h"
#include "FactoryPresets.h"
#include "../Parameters.h"

namespace pluck
{

//==============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    for (const auto& id : ParamID::all)
        apvts.addParameterListener (id, this);

    if (getCurrentPresetName().isEmpty())
        setCurrentPresetName (getFactoryPresetName (0));
}

PresetManager::~PresetManager()
{
    for (const auto& id : ParamID::all)
        apvts.removeParameterListener (id, this);
}

//==============================================================================
juce::File PresetManager::getUserPresetDirectory()
{
    // Folder names come from the build system's JucePlugin_Manufacturer and JucePlugin_Name.
   #if JUCE_MAC
    // Standard macOS audio preset location.
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library/Audio/Presets")
               .getChildFile (JucePlugin_Manufacturer)
               .getChildFile (JucePlugin_Name);
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (JucePlugin_Manufacturer)
               .getChildFile (JucePlugin_Name)
               .getChildFile ("Presets");
   #endif
}

//==============================================================================
int PresetManager::getNumFactoryPresets() const
{
    return (int) getFactoryPresets().size();
}

juce::String PresetManager::getFactoryPresetName (int index) const
{
    const auto& presets = getFactoryPresets();

    if (juce::isPositiveAndBelow (index, (int) presets.size()))
        return presets[(size_t) index].name;

    return {};
}

void PresetManager::loadFactoryPreset (int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    const auto& preset = presets[(size_t) index];

    ValueMap values;
    for (const auto& [id, value] : preset.values)
        values[id] = value;

    applyValues (values, preset.name);
}

//==============================================================================
juce::StringArray PresetManager::getUserPresetNames() const
{
    juce::StringArray names;

    for (const auto& file : getUserPresetDirectory().findChildFiles (juce::File::findFiles, false,
                                                                     "*" + juce::String (fileExtension)))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    return fileForPreset (name).existsAsFile();
}

bool PresetManager::isFactoryPresetName (const juce::String& name) const
{
    for (const auto& preset : getFactoryPresets())
        if (name.equalsIgnoreCase (preset.name))
            return true;

    return false;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);

    if (name.isEmpty() || isFactoryPresetName (name))
        return false;

    juce::XmlElement root (xmlRootTag);
    root.setAttribute ("name", name);
    root.setAttribute ("version", JucePlugin_VersionString);

    for (const auto& id : ParamID::all)
    {
        if (auto* param = apvts.getParameter (id))
        {
            auto* element = root.createNewChildElement (xmlParamTag);
            element->setAttribute ("id", id);
            element->setAttribute ("value", (double) param->convertFrom0to1 (param->getValue()));
        }
    }

    const auto file = fileForPreset (name);

    if (! file.getParentDirectory().createDirectory())
        return false;

    if (! root.writeTo (file))
        return false;

    setCurrentPresetName (name);
    modified.store (false);
    return true;
}

bool PresetManager::loadUserPreset (const juce::String& name)
{
    const auto file = fileForPreset (name);
    const auto xml  = juce::parseXML (file);

    if (xml == nullptr || ! xml->hasTagName (xmlRootTag))
        return false;

    ValueMap values;

    for (auto* element : xml->getChildWithTagNameIterator (xmlParamTag))
        values[element->getStringAttribute ("id")] = (float) element->getDoubleAttribute ("value");

    // Use the file name, not the stored attribute, so renamed files show their new name.
    applyValues (values, file.getFileNameWithoutExtension());
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    const auto file = fileForPreset (name);

    if (! file.existsAsFile())
        return false;

    const bool wasCurrent = name == getCurrentPresetName();

    if (! file.deleteFile())
        return false;

    if (wasCurrent)
    {
        // Values are unchanged; fall back to the first factory name and mark as modified.
        setCurrentPresetName (getFactoryPresetName (0));
        modified.store (true);
    }

    return true;
}

//==============================================================================
juce::StringArray PresetManager::getAllPresetNames() const
{
    juce::StringArray names;

    for (const auto& preset : getFactoryPresets())
        names.add (preset.name);

    names.addArray (getUserPresetNames());
    return names;
}

int PresetManager::getCurrentPresetIndex() const
{
    return getAllPresetNames().indexOf (getCurrentPresetName());
}

void PresetManager::loadPresetAtIndex (int index)
{
    const auto numFactory = getNumFactoryPresets();

    if (index < numFactory)
    {
        loadFactoryPreset (index);
        return;
    }

    const auto userNames = getUserPresetNames();
    const auto userIndex = index - numFactory;

    if (juce::isPositiveAndBelow (userIndex, userNames.size()))
        loadUserPreset (userNames[userIndex]);
}

void PresetManager::loadNextPreset()
{
    const auto total = getAllPresetNames().size();
    if (total == 0) return;

    loadPresetAtIndex ((getCurrentPresetIndex() + 1) % total);
}

void PresetManager::loadPreviousPreset()
{
    const auto total = getAllPresetNames().size();
    if (total == 0) return;

    const auto current = getCurrentPresetIndex();
    loadPresetAtIndex (current <= 0 ? total - 1 : current - 1);
}

//==============================================================================
juce::String PresetManager::getCurrentPresetName() const
{
    return apvts.state.getProperty (presetNameProperty).toString();
}

void PresetManager::stateRestored()
{
    if (getCurrentPresetName().isEmpty())
        setCurrentPresetName (getFactoryPresetName (0));

    modified.store (false);
}

//==============================================================================
void PresetManager::applyValues (const ValueMap& values, const juce::String& presetName)
{
    // parameterChanged() fires synchronously from setValueNotifyingHost(); suppress the modified flag.
    applyingPreset = true;

    for (const auto& id : ParamID::all)
    {
        auto* param = apvts.getParameter (id);
        if (param == nullptr)
            continue;

        const auto it = values.find (id);
        const auto normalised = it != values.end() ? param->convertTo0to1 (it->second)
                                                   : param->getDefaultValue();

        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    }

    applyingPreset = false;

    setCurrentPresetName (presetName);
    modified.store (false);
}

void PresetManager::setCurrentPresetName (const juce::String& name)
{
    apvts.state.setProperty (presetNameProperty, name, nullptr);
}

juce::File PresetManager::fileForPreset (const juce::String& name)
{
    return getUserPresetDirectory().getChildFile (sanitiseName (name) + fileExtension);
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name).trim();
}

void PresetManager::parameterChanged (const juce::String&, float)
{
    if (! applyingPreset)
        modified.store (true);
}

} // namespace pluck
