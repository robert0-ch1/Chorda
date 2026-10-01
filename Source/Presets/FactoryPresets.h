/*
  ==============================================================================

    FactoryPresets.h

    Built-in presets as tables of parameter ID / value pairs in real units.
    Parameters a preset omits fall back to their defaults.

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

/** Built-in presets in UI order; index 0 is "Init". */
const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace pluck
