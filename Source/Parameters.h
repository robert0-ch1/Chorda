/*
  ==============================================================================

    Parameters.h

    Parameter IDs, ranges, defaults and value-to-text functions for Chorda.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck
{

//==============================================================================
/** Parameter IDs. Stored in presets and host sessions: do not rename. */
namespace ParamID
{
    // Exciter
    inline constexpr auto exciterTone     = "exciterTone";      // 0..1: sine, square, noise
    inline constexpr auto exciterAttack   = "exciterAttack";    // ms, excitation length
    inline constexpr auto exciterPosition = "exciterPosition";  // 0..1, bridge to nut

    // String loop
    inline constexpr auto stringDecay      = "stringDecay";      // s, T60 while held
    inline constexpr auto stringSustain    = "stringSustain";    // 0..1, hold level, see sustainRangeDb
    inline constexpr auto stringRelease    = "stringRelease";    // s, T60 after note-off
    inline constexpr auto stringBrightness = "stringBrightness"; // Hz, loop low-pass cutoff
    inline constexpr auto stringDamper     = "stringDamper";     // 0..1, finger position (0 = off)
    inline constexpr auto stringDamperPressure = "stringDamperPressure"; // 0..1
    inline constexpr auto stringSub        = "stringSub";        // 0..1, sine one octave down

    // Voices
    inline constexpr auto voiceMode   = "voiceMode";    // choice, see voiceModeNames
    inline constexpr auto pitchGlide  = "pitchGlide";   // s
    inline constexpr auto pitchOctave = "pitchOctave";  // choice, see octaveNames

    // LFOs: damper position and damper pressure
    inline constexpr auto lfoAmount         = "lfoAmount";          // 0..1, bipolar around the set position
    inline constexpr auto lfoRate           = "lfoRate";            // Hz, or a note division when synced
    inline constexpr auto lfoSync           = "lfoSync";            // bool, follow host tempo
    inline constexpr auto lfoPressureAmount = "lfoPressureAmount";  // 0..1, bipolar around the set pressure
    inline constexpr auto lfoPressureRate   = "lfoPressureRate";
    inline constexpr auto lfoPressureSync   = "lfoPressureSync";

    // Output
    inline constexpr auto outputDrive     = "outputDrive";     // 0..1, saturation
    inline constexpr auto outputWidth     = "outputWidth";     // 0..1, 0 = mono
    inline constexpr auto outputGain      = "outputGain";      // dB
    inline constexpr auto outputReverb    = "outputReverb";    // 0..1, send level, last in chain

    /** All IDs, in display order. */
    inline const juce::StringArray all
    {
        exciterTone, exciterAttack, exciterPosition,
        stringDecay, stringSustain, stringRelease, stringBrightness, stringDamper, stringDamperPressure, stringSub,
        voiceMode, pitchGlide, pitchOctave,
        lfoAmount, lfoRate, lfoSync, lfoPressureAmount, lfoPressureRate, lfoPressureSync,
        outputWidth, outputDrive, outputReverb, outputGain
    };
}

/** Tone knob anchor points; waveforms are crossfaded in between. */
inline constexpr float toneSine   = 0.0f;
inline constexpr float toneSquare = 0.5f;
inline constexpr float toneNoise  = 1.0f;

//==============================================================================
/** voiceMode choices: Mono, Legato, then polyphony limits. Legato is mono
    without retrigger: an overlapping key glides the ringing string. */
inline const juce::StringArray voiceModeNames { "Mono", "Legato", "2", "3", "4", "5", "6", "7", "8", "16", "32", "64" };

inline constexpr int voiceModeMono   = 0;
inline constexpr int voiceModeLegato = 1;

/** pitchOctave choices, in octaves. */
inline const juce::StringArray octaveNames { "-2", "-1", "0", "+1", "+2" };
inline constexpr int octaveDefaultIndex = 2;

/** Semitone shift for a pitchOctave index. */
inline int semitonesForOctaveIndex (int index)
{
    return 12 * octaveNames[juce::jlimit (0, octaveNames.size() - 1, index)].getIntValue();
}

/** Velocity scaling of attack time and brightness, at velocity 0 and 1,
    linear in between. A hard pluck is shorter and brighter. */
inline constexpr float velocityAttackFactorSoft   = 1.6f;   ///< longer excitation
inline constexpr float velocityAttackFactorHard   = 0.8f;
inline constexpr float velocityBrightnessSoft     = 0.5f;   ///< darker
inline constexpr float velocityBrightnessHard     = 1.25f;

/** Bipolar LFO depth at full Amount. 0.5 lets the position reach the whole string. */
inline constexpr float lfoPositionDepth = 0.5f;
inline constexpr float lfoPressureDepth = 0.5f;

/** Host-synced LFO divisions, in beats per cycle, longest first. */
inline const std::array<float, 11> lfoDivisionBeats { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
inline const std::array<const char*, 11> lfoDivisionNames { "4/1", "2/1", "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

/** Maximum mod wheel brightness boost, in octaves. */
inline constexpr float modWheelBrightnessOctaves = 2.0f;

/** Polyphony for a voiceMode index. */
inline int polyphonyForVoiceMode (int modeIndex)
{
    if (modeIndex <= voiceModeLegato)
        return 1;
    return voiceModeNames[juce::jlimit (0, voiceModeNames.size() - 1, modeIndex)].getIntValue();
}

/** Allocated voices, enough for the largest mode. */
inline constexpr int numVoices = 64;

/** Pitch-bend range in semitones, symmetric. */
inline constexpr float pitchBendRangeSemitones = 2.0f;

/** Lower pick position bound. Zero would remove the pick comb entirely. */
inline constexpr float pickPositionMinimum = 0.005f;   // shown as 1 %

/** Positions run bridge (0) to nut (1). Mode shapes are symmetric about the
    middle, so p and 1 - p excite and damp the same harmonics. */
inline constexpr float positionMaximum = 1.0f - pickPositionMinimum;

/** Position folded about the middle of the string. */
inline float mirroredPosition (float position)   { return juce::jmin (position, 1.0f - position); }

/** Hold level is -sustainRangeDb * (1 - sustain) dB, reached after
    (1 - sustain) * decay seconds. Sustain 0 never holds. */
inline constexpr float sustainRangeDb = 60.0f;

//==============================================================================
namespace detail
{
    /** Logarithmic range: equal knob travel per octave. */
    inline juce::NormalisableRange<float> logRange (float min, float max)
    {
        return { min, max,
                 [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                 [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); } };
    }

    inline juce::String hzToText (float hz, int)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    inline juce::String msToText (float ms, int)
    {
        if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
        if (ms >= 10.0f)   return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms, 1) + " ms";
    }

    inline juce::String secondsToText (float s, int)
    {
        if (s < 1.0f)   return juce::String (juce::roundToInt (s * 1000.0f)) + " ms";
        return s >= 10.0f ? juce::String (s, 1) + " s" : juce::String (s, 2) + " s";
    }

    inline juce::String percentToText (float v, int)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    /** Glide time, "off" at the minimum. */
    inline juce::String glideToText (float v, int)
    {
        return v <= 0.0015f ? juce::String ("off") : secondsToText (v, 0);
    }

    inline juce::String rateToText (float v, int)
    {
        return juce::String (v, v < 10.0f ? 2 : 1) + " Hz";
    }

    inline juce::String toneToText (float v, int)
    {
        if (v <= toneSine + 0.005f)                  return "sine";
        if (std::abs (v - toneSquare) < 0.005f)      return "square";
        if (v >= toneNoise - 0.005f)                 return "noise";

        const auto from = v < toneSquare ? "sine to square " : "square to noise ";
        const auto t = v < toneSquare ? v / toneSquare : (v - toneSquare) / (toneNoise - toneSquare);
        return from + juce::String (juce::roundToInt (t * 100.0f)) + " %";
    }

    inline juce::String positionToText (float v, int)
    {
        return mirroredPosition (v) <= 0.0005f ? juce::String ("off") : juce::String (juce::roundToInt (v * 100.0f)) + " %";
    }

    inline juce::String dbToText (float db, int)
    {
        return db <= -59.9f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }

    using Attr = juce::AudioParameterFloatAttributes;

    inline std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const juce::String& name,
                                                                 juce::NormalisableRange<float> range, float def,
                                                                 juce::String (*toText) (float, int))
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def,
                                                            Attr().withStringFromValueFunction (toText));
    }
}

//==============================================================================
/** Builds the parameter tree that AudioProcessorValueTreeState is constructed with. */
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace detail;
    using Group = juce::AudioProcessorParameterGroup;

    auto exciter = std::make_unique<Group> ("exciter", "Exciter", "|",
        makeFloat (ParamID::exciterTone,     "Exciter",          { 0.0f, 1.0f, 0.001f },   toneNoise, toneToText),
        makeFloat (ParamID::exciterAttack,   "Attack",        logRange (0.5f, 2000.0f), 3.0f, msToText),
        makeFloat (ParamID::exciterPosition, "Pick Position", { pickPositionMinimum, positionMaximum, 0.001f }, 0.2f, positionToText));

    auto string = std::make_unique<Group> ("string", "String", "|",
        makeFloat (ParamID::stringDecay,      "Decay",           logRange (0.05f, 20.0f),     2.0f,    secondsToText),
        makeFloat (ParamID::stringSustain,    "Sustain",         { 0.0f, 1.0f, 0.001f },      0.0f,    percentToText),
        makeFloat (ParamID::stringRelease,    "Release",         logRange (0.01f, 10.0f),     0.3f,    secondsToText),
        makeFloat (ParamID::stringBrightness, "Brightness",      logRange (200.0f, 20000.0f), 5000.0f, hzToText),
        makeFloat (ParamID::stringDamper,     "Damper Position", { 0.0f, 1.0f, 0.001f },      0.0f,    positionToText),
        makeFloat (ParamID::stringDamperPressure, "Damper Pressure", { 0.0f, 1.0f, 0.001f },  0.6f,    percentToText),
        makeFloat (ParamID::stringSub,        "Sub",             { 0.0f, 1.0f, 0.001f },      0.0f,    percentToText));

    auto voices = std::make_unique<Group> ("voices", "Voices", "|",
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::voiceMode, 1 },
                                                      "Voice Mode", voiceModeNames, voiceModeNames.indexOf ("16")),
        makeFloat (ParamID::pitchGlide, "Glide", logRange (0.001f, 2.0f), 0.001f, glideToText),
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::pitchOctave, 1 },
                                                      "Octave", octaveNames, octaveDefaultIndex));

    auto lfo = std::make_unique<Group> ("lfo", "LFO", "|",
        makeFloat (ParamID::lfoAmount,         "Position LFO Amount", { 0.0f, 1.0f, 0.001f }, 0.0f, percentToText),
        makeFloat (ParamID::lfoRate,           "Position LFO Rate",   logRange (0.05f, 20.0f), 1.0f, rateToText),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamID::lfoSync, 1 }, "Position LFO Sync", false),
        makeFloat (ParamID::lfoPressureAmount, "Pressure LFO Amount", { 0.0f, 1.0f, 0.001f }, 0.0f, percentToText),
        makeFloat (ParamID::lfoPressureRate,   "Pressure LFO Rate",   logRange (0.05f, 20.0f), 1.0f, rateToText),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamID::lfoPressureSync, 1 }, "Pressure LFO Sync", false));

    auto output = std::make_unique<Group> ("output", "Output", "|",
        makeFloat (ParamID::outputWidth,      "Width",       { 0.0f, 1.0f, 0.001f },  0.0f, percentToText),
        makeFloat (ParamID::outputDrive,      "Drive",       { 0.0f, 1.0f, 0.001f },  0.0f, percentToText),
        makeFloat (ParamID::outputReverb,     "Reverb Send", { 0.0f, 1.0f, 0.001f },  0.0f, percentToText),
        makeFloat (ParamID::outputGain,       "Gain",        { -60.0f, 12.0f, 0.1f }, 0.0f, dbToText));

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::move (exciter), std::move (string), std::move (voices), std::move (lfo), std::move (output));
    return layout;
}

} // namespace pluck
