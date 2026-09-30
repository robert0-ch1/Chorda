/*
  ==============================================================================

    FactoryPresets.h

    Presets that ship inside the binary. They are plain tables of
    parameter-ID / value pairs (values in real units: ms, Hz, dB...), so adding
    a preset is a matter of appending one entry to FactoryPresets.cpp.

    Parameters that a preset does not mention fall back to their defaults.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>

namespace pluck
{

struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

/** The built-in presets, in the order they appear in the UI. Index 0 is "Init". */
const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace pluck
