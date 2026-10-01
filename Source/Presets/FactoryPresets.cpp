/*
  ==============================================================================

    FactoryPresets.cpp

    Units match the UI: attack in ms; decay and release in s; brightness and
    LFO rates in Hz; percentages as 0..1; gain in dB. Positions run 0 (bridge)
    to 1 (nut). Choices are option indices: voiceMode (0 Mono, 1 Legato,
    8 = 8 voices, 9 = 16), pitchOctave (1 = -1, 2 = 0, 3 = +1).
    Tone: 0 sine, 0.5 square, 1 noise burst.

  ==============================================================================
*/

#include "FactoryPresets.h"
#include "../Parameters.h"

namespace pluck
{

const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamID;

    static const std::vector<FactoryPreset> presets
    {
        // All defaults
        { "Init", {} },

        // --- Basses ------------------------------------------------------------
        { "Sub Bass",
          { { exciterTone, 0.245f }, { exciterAttack, 4.82082f }, { exciterPosition, 0.883f },
            { stringDecay, 20.0f }, { stringSustain, 1.0f }, { stringRelease, 10.0f },
            { stringBrightness, 747.522f }, { stringDamper, 0.0f }, { stringDamperPressure, 1.0f },
            { stringSub, 1.0f }, { voiceMode, 0.0f }, { pitchGlide, 0.001f }, { pitchOctave, 0.0f },
            { lfoAmount, 0.0f }, { lfoRate, 1.0f }, { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.0f }, { outputDrive, 0.0f },
            { outputReverb, 0.0f }, { outputGain, 0.0f } } },

        { "Picked Bass",
          { { exciterTone, 0.565f }, { exciterAttack, 2.0f }, { exciterPosition, 0.08f }, { stringDecay, 1.8f },
            { stringSustain, 0.0f }, { stringRelease, 0.1f }, { stringBrightness, 6383.42f },
            { stringDamper, 0.06f }, { stringDamperPressure, 0.3f }, { stringSub, 1.0f }, { voiceMode, 0.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 0.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 0.0f }, { outputDrive, 0.15f }, { outputReverb, 0.0f }, { outputGain, 7.0f } } },

        { "Glide Bass",
          { { exciterTone, 0.5f }, { exciterAttack, 85.6605f }, { exciterPosition, 0.2f }, { stringDecay, 4.0f },
            { stringSustain, 1.0f }, { stringRelease, 0.2f }, { stringBrightness, 1200.0f },
            { stringDamper, 0.0f }, { stringDamperPressure, 0.6f }, { stringSub, 0.737f }, { voiceMode, 1.0f },
            { pitchGlide, 0.08f }, { pitchOctave, 1.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 0.339f }, { outputDrive, 0.442f }, { outputReverb, 0.0f }, { outputGain, 0.0f } } },

        // --- Guitars -----------------------------------------------------------
        { "Nylon Guitar",
          { { exciterTone, 0.871f }, { exciterAttack, 4.01253f }, { exciterPosition, 0.206f },
            { stringDecay, 20.0f }, { stringSustain, 0.0f }, { stringRelease, 0.252519f },
            { stringBrightness, 200.0f }, { stringDamper, 0.0f }, { stringDamperPressure, 0.6f },
            { stringSub, 0.939f }, { voiceMode, 8.0f }, { pitchGlide, 0.001f }, { pitchOctave, 3.0f },
            { lfoAmount, 0.0f }, { lfoRate, 1.0f }, { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.2f }, { outputDrive, 0.194f },
            { outputReverb, 0.092f }, { outputGain, -0.499999f } } },

        { "Steel Guitar",
          { { exciterTone, 1.0f }, { exciterAttack, 1.5f }, { exciterPosition, 0.14f }, { stringDecay, 5.0f },
            { stringSustain, 0.0f }, { stringRelease, 0.35f }, { stringBrightness, 5849.15f },
            { stringDamper, 0.0f }, { stringDamperPressure, 0.0f }, { stringSub, 0.095f }, { voiceMode, 8.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 2.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 0.153f }, { outputDrive, 0.193f }, { outputReverb, 0.18f }, { outputGain, -3.0f } } },

        { "Electric Guitar",
          { { exciterTone, 0.9f }, { exciterAttack, 2.0f }, { exciterPosition, 0.1f }, { stringDecay, 6.0f },
            { stringSustain, 0.5f }, { stringRelease, 0.3f }, { stringBrightness, 6672.97f },
            { stringDamper, 0.0f }, { stringDamperPressure, 0.6f }, { stringSub, 0.327f }, { voiceMode, 4.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 1.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 1.0f }, { outputDrive, 0.507f }, { outputReverb, 0.104f }, { outputGain, -6.0f } } },

        { "Harp",
          { { exciterTone, 0.882f }, { exciterAttack, 16.4146f }, { exciterPosition, 0.5f },
            { stringDecay, 1.33313f }, { stringSustain, 0.175f }, { stringRelease, 1.44854f },
            { stringBrightness, 11515.4f }, { stringDamper, 0.0f }, { stringDamperPressure, 1.0f },
            { stringSub, 0.0f }, { voiceMode, 9.0f }, { pitchGlide, 0.001f }, { pitchOctave, 3.0f },
            { lfoAmount, 0.117f }, { lfoRate, 1.05461f }, { lfoSync, 1.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.185f }, { outputDrive, 0.0f },
            { outputReverb, 0.212f }, { outputGain, -0.399999f } } },

        { "Koto",
          { { exciterTone, 0.569f }, { exciterAttack, 4.0f }, { exciterPosition, 0.1f }, { stringDecay, 2.0f },
            { stringSustain, 0.0f }, { stringRelease, 0.15f }, { stringBrightness, 6998.9f },
            { stringDamper, 0.33f }, { stringDamperPressure, 0.15f }, { stringSub, 0.0f }, { voiceMode, 8.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 2.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 0.432f }, { outputDrive, 0.366f }, { outputReverb, 0.14f }, { outputGain, 2.0f } } },

        // --- Keys and leads ----------------------------------------------------
        { "Electric Piano",
          { { exciterTone, 0.452f }, { exciterAttack, 2.60427f }, { exciterPosition, 0.288f },
            { stringDecay, 3.5f }, { stringSustain, 0.902f }, { stringRelease, 0.5f },
            { stringBrightness, 2522.75f }, { stringDamper, 0.549f }, { stringDamperPressure, 0.3f },
            { stringSub, 0.735f }, { voiceMode, 9.0f }, { pitchGlide, 0.001f }, { pitchOctave, 3.0f },
            { lfoAmount, 0.05f }, { lfoRate, 0.395772f }, { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.0f }, { outputDrive, 0.0f },
            { outputReverb, 0.2f }, { outputGain, 0.0f } } },

        { "Harmonic Bells",
          { { exciterTone, 0.0f }, { exciterAttack, 10.0f }, { exciterPosition, 0.2f }, { stringDecay, 20.0f },
            { stringSustain, 0.0f }, { stringRelease, 3.0f }, { stringBrightness, 14000.0f },
            { stringDamper, 0.25f }, { stringDamperPressure, 0.9f }, { stringSub, 0.0f }, { voiceMode, 9.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 3.0f }, { lfoAmount, 0.0f }, { lfoRate, 1.0f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f }, { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f },
            { outputWidth, 0.7f }, { outputDrive, 0.0f }, { outputReverb, 0.4f }, { outputGain, 1.1f } } },

        { "Mono Lead",
          { { exciterTone, 0.443f }, { exciterAttack, 6.41372f }, { exciterPosition, 0.252f },
            { stringDecay, 3.44941f }, { stringSustain, 1.0f }, { stringRelease, 1.47251f },
            { stringBrightness, 8308.64f }, { stringDamper, 0.507f }, { stringDamperPressure, 0.237f },
            { stringSub, 0.62f }, { voiceMode, 1.0f }, { pitchGlide, 0.06f }, { pitchOctave, 2.0f },
            { lfoAmount, 0.029f }, { lfoRate, 2.87723f }, { lfoSync, 1.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.377f }, { outputDrive, 0.63f },
            { outputReverb, 0.106f }, { outputGain, 0.0f } } },

        // --- Pads and movement -------------------------------------------------
        { "Bowed Pad",
          { { exciterTone, 0.704f }, { exciterAttack, 2000.0f }, { exciterPosition, 0.3f }, { stringDecay, 20.0f },
            { stringSustain, 1.0f }, { stringRelease, 2.5f }, { stringBrightness, 5955.32f },
            { stringDamper, 0.35f }, { stringDamperPressure, 0.3f }, { stringSub, 0.019f }, { voiceMode, 9.0f },
            { pitchGlide, 0.001f }, { pitchOctave, 2.0f }, { lfoAmount, 0.25f }, { lfoRate, 0.15f },
            { lfoSync, 0.0f }, { lfoPressureAmount, 0.172f }, { lfoPressureRate, 0.299205f },
            { lfoPressureSync, 0.0f }, { outputWidth, 0.9f }, { outputDrive, 0.1f }, { outputReverb, 0.5f },
            { outputGain, -4.5f } } },

        { "Halo",
          { { exciterTone, 0.284f }, { exciterAttack, 750.238f }, { exciterPosition, 0.15f },
            { stringDecay, 15.0f }, { stringSustain, 1.0f }, { stringRelease, 3.0f },
            { stringBrightness, 8803.49f }, { stringDamper, 0.25f }, { stringDamperPressure, 0.35f },
            { stringSub, 0.76f }, { voiceMode, 9.0f }, { pitchGlide, 0.001f }, { pitchOctave, 2.0f },
            { lfoAmount, 0.0f }, { lfoRate, 1.0f }, { lfoSync, 0.0f }, { lfoPressureAmount, 0.638f },
            { lfoPressureRate, 0.2f }, { lfoPressureSync, 0.0f }, { outputWidth, 1.0f }, { outputDrive, 0.262f },
            { outputReverb, 0.79f }, { outputGain, 0.0f } } },

        { "Tempo Pulse",
          { { exciterTone, 0.759f }, { exciterAttack, 0.5f }, { exciterPosition, 0.493f }, { stringDecay, 8.0f },
            { stringSustain, 1.0f }, { stringRelease, 0.4f }, { stringBrightness, 4416.35f },
            { stringDamper, 0.5f }, { stringDamperPressure, 0.5f }, { stringSub, 1.0f }, { voiceMode, 8.0f },
            { pitchGlide, 0.0058917f }, { pitchOctave, 2.0f }, { lfoAmount, 1.0f }, { lfoRate, 0.571313f },
            { lfoSync, 1.0f }, { lfoPressureAmount, 0.695f }, { lfoPressureRate, 0.790514f },
            { lfoPressureSync, 1.0f }, { outputWidth, 0.402f }, { outputDrive, 0.0f }, { outputReverb, 0.381f },
            { outputGain, 1.9f } } },

        { "Muted Pluck",
          { { exciterTone, 0.834f }, { exciterAttack, 0.8f }, { exciterPosition, 0.12f },
            { stringDecay, 1.60094f }, { stringSustain, 0.0f }, { stringRelease, 0.551975f },
            { stringBrightness, 1575.55f }, { stringDamper, 0.06f }, { stringDamperPressure, 0.4f },
            { stringSub, 0.0f }, { voiceMode, 9.0f }, { pitchGlide, 0.001f }, { pitchOctave, 2.0f },
            { lfoAmount, 0.0f }, { lfoRate, 1.0f }, { lfoSync, 0.0f }, { lfoPressureAmount, 0.0f },
            { lfoPressureRate, 1.0f }, { lfoPressureSync, 0.0f }, { outputWidth, 0.0f }, { outputDrive, 0.291f },
            { outputReverb, 0.0f }, { outputGain, 5.0f } } },
    };

    return presets;
}

} // namespace pluck
