/*
  ==============================================================================

    PluginProcessor.h

    Chorda audio processor. Chain: voices (mono) -> drive (2x oversampled)
    -> gain -> width -> reverb send -> safety limiter. Two global LFOs modulate
    damper position and pressure on all voices.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "Parameters.h"
#include "DSP/KarplusVoice.h"
#include "DSP/PluckSynth.h"
#include "DSP/StereoWidener.h"
#include "Presets/PresetManager.h"

//==============================================================================
class ChordaAudioProcessor final : public juce::AudioProcessor
{
public:
    ChordaAudioProcessor();
    ~ChordaAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;   // double-precision overload stays the base version

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    //==============================================================================
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override;

    //==============================================================================
    // Single host program; presets go through PresetManager.
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept     { return apvts; }
    pluck::PresetManager& getPresetManager() noexcept           { return presetManager; }
    juce::MidiKeyboardState& getKeyboardState() noexcept        { return keyboardState; }

    /** Peak of the voice sum in the last block, before the master chain. */
    float getStringLevel() const noexcept                       { return stringLevel.load(); }

    /** Note-ons received since construction. */
    int getNoteOnCount() const noexcept                         { return noteOnCount.load(); }

    /** Voices currently sounding. */
    int getActiveVoiceCount() const noexcept                    { return activeVoiceCount.load(); }

    /** Output peak of the last block. */
    float getOutputPeak() const noexcept                        { return outputPeak.load(); }

    /** LFO rate as text: Hz, or the note division when synced. */
    juce::String getLfoRateText (bool pressure = false) const;

    /** Last modulated damper position, or -1 when the LFO is idle. */
    float getDamperModulation() const noexcept                  { return positionLfo.now.load(); }

    /** Last modulated damper pressure, or -1 when the LFO is idle. */
    float getPressureModulation() const noexcept                { return pressureLfo.now.load(); }

private:
    //==============================================================================
    void updateVoiceParameters();
    void applyDrive (juce::AudioBuffer<float>& mono);
    void applyGain (float* mono, int numSamples);
    void applyWidth (juce::AudioBuffer<float>& output, const float* mono, int numSamples);
    void applyReverb (juce::AudioBuffer<float>& output, int numSamples);
    void applySafetyLimiter (juce::AudioBuffer<float>& output, int numSamples);

    /** Bipolar sine LFO on a damper parameter: per-sample output centred on
        the parameter value, swing Amount * depth. */
    struct DamperLfo
    {
        std::atomic<float>* amount = nullptr;
        std::atomic<float>* rate   = nullptr;
        std::atomic<float>* sync   = nullptr;
        std::atomic<float>* centre = nullptr;   // modulated parameter
        float depth = 0.25f, lowest = 0.0f, highest = 1.0f;

        std::vector<float> buffer;
        bool   active = false;
        double phase = 0.0;                     // 0..1
        float  smoothed = 0.0f;                 // sine after one-pole smoothing of phase jumps
        juce::SmoothedValue<float> amountSmoothed;
        std::atomic<float> now { -1.0f };
    };

    /** Renders one block of LFO output. Returns false when idle. */
    bool renderLfo (DamperLfo&, int numSamples, const juce::Optional<juce::AudioPlayHead::PositionInfo>& position);

    std::atomic<float>* rawParameter (const char* id) const;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    pluck::PresetManager presetManager;      // must follow apvts, holds a reference
    juce::MidiKeyboardState keyboardState;   // shared with the editor keyboard

    /** Beats per cycle when synced: nearest division to the Rate value. */
    float lfoSyncedBeats (float rateHz) const;

    std::atomic<double> hostBpm { 120.0 };

    pluck::PluckSynth synth;
    std::vector<pluck::KarplusVoice*> voices;   // owned by synth, cached to avoid dynamic_cast

    /** Parameter atomics, resolved in the constructor. */
    struct RawParameters
    {
        std::atomic<float>* exciterTone      = nullptr;
        std::atomic<float>* exciterAttack    = nullptr;
        std::atomic<float>* exciterPosition  = nullptr;
        std::atomic<float>* stringDecay      = nullptr;
        std::atomic<float>* stringSustain    = nullptr;
        std::atomic<float>* stringRelease    = nullptr;
        std::atomic<float>* stringBrightness = nullptr;
        std::atomic<float>* stringDamper     = nullptr;
        std::atomic<float>* stringDamperPressure = nullptr;
        std::atomic<float>* stringSub        = nullptr;
        std::atomic<float>* voiceMode        = nullptr;
        std::atomic<float>* pitchGlide       = nullptr;
        std::atomic<float>* pitchOctave      = nullptr;
        std::atomic<float>* outputDrive      = nullptr;
        std::atomic<float>* outputWidth      = nullptr;
        std::atomic<float>* outputGain       = nullptr;
        std::atomic<float>* outputReverb     = nullptr;
    } raw;

    // Master chain, mono until the widener
    juce::AudioBuffer<float> synthBuffer;    // voice sum
    juce::dsp::Oversampling<float> oversampling { 1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    float driveDcIn1 = 0.0f, driveDcOut1 = 0.0f, driveDcCoeff = 0.999f;
    juce::SmoothedValue<float> driveSmoothed;
    juce::SmoothedValue<float> outputGainSmoothed;
    pluck::StereoWidener widener;                   // mono -> stereo

    // Reverb send: wet is added to an untouched dry path, so send 0 is bit-transparent.
    juce::Reverb reverb;
    juce::AudioBuffer<float> reverbBuffer;
    juce::SmoothedValue<float> reverbSendSmoothed;
    bool reverbRunning = false;

    // Safety limiter: ceiling +6 dBFS, non-finite samples are zeroed.
    float limiterGain = 1.0f;
    float limiterRelease = 0.001f;

    // Damper LFOs
    DamperLfo positionLfo, pressureLfo;
    float lfoSmoothingCoeff = 0.01f;
    float modWheel = 0.0f;                   // CC1, 0..1

    // Telemetry for the editor
    std::atomic<float> stringLevel { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int>   activeVoiceCount { 0 };
    std::atomic<int>   noteOnCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordaAudioProcessor)
};
