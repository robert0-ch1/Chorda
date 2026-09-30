/*
  ==============================================================================

    PluginProcessor.h

    The audio side of Chorda.

    A PluckSynth (juce::Synthesiser with a polyphony limit) owns 64 KarplusVoice
    instances and turns MIDI into a mono string signal. That signal then runs
    through a short master chain:

        voices -> valve drive (2x oversampled) -> gain -> stereo width -> reverb -> safety limiter -> outputs

    An LFO runs alongside, once for all the voices, and moves the damper
    along every string together. It can lock to the host's tempo and bar.

    Parameters live in an AudioProcessorValueTreeState; the processor reads the
    atomic raw values once per block. Presets and host state are handled by
    PresetManager and get/setStateInformation respectively.

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
    // Presets are exposed through PresetManager rather than the host program
    // list, so there is a single program. Mixing the two mechanisms confuses
    // some hosts (they overwrite the program name with their own).
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

    /** Peak level of the summed strings in the last block (before the tone
        controls). Read by the UI to animate the string drawing. */
    float getStringLevel() const noexcept                       { return stringLevel.load(); }

    /** How many note-ons have arrived since the plugin started. The UI plays
        the pick hand's plucking animation each time it goes up. */
    int getNoteOnCount() const noexcept                         { return noteOnCount.load(); }

    /** Voices currently sounding. */
    int getActiveVoiceCount() const noexcept                    { return activeVoiceCount.load(); }

    /** Peak of the last output block after the master gain, for the meter. */
    float getOutputPeak() const noexcept                        { return outputPeak.load(); }

    /** How an LFO rate reads on its knob: a frequency, or the note division
        it has snapped to when it is following the host. */
    juce::String getLfoRateText (bool pressure = false) const;

    /** Where the position LFO last put the damper, as a fraction of the
        string, or -1 while it is idle. Read by the UI and the tests. */
    float getDamperModulation() const noexcept                  { return positionLfo.now.load(); }

    /** The pressure the pressure LFO last asked for, or -1 while it is idle. */
    float getPressureModulation() const noexcept                { return pressureLfo.now.load(); }

private:
    //==============================================================================
    void updateVoiceParameters();
    void applyDrive (juce::AudioBuffer<float>& mono);
    void applyGain (float* mono, int numSamples);
    void applyWidth (juce::AudioBuffer<float>& output, const float* mono, int numSamples);
    void applyReverb (juce::AudioBuffer<float>& output, int numSamples);
    void applySafetyLimiter (juce::AudioBuffer<float>& output, int numSamples);

    /** One bipolar sine LFO on a damper parameter. It fills a buffer with
        the value it asks for, sample by sample, centred on where the knob or
        marker has it and swinging Amount x depth either side. */
    struct DamperLfo
    {
        std::atomic<float>* amount = nullptr;
        std::atomic<float>* rate   = nullptr;
        std::atomic<float>* sync   = nullptr;
        std::atomic<float>* centre = nullptr;   // the parameter it moves
        float depth = 0.25f, lowest = 0.0f, highest = 1.0f;

        std::vector<float> buffer;
        bool   active = false;
        double phase = 0.0;                     // 0..1
        float  smoothed = 0.0f;                 // the sine, with any jump in phase rounded off
        juce::SmoothedValue<float> amountSmoothed;
        std::atomic<float> now { -1.0f };
    };

    /** Fills an LFO's buffer for the block. Returns false when it is idle,
        so the voices can skip it. */
    bool renderLfo (DamperLfo&, int numSamples, const juce::Optional<juce::AudioPlayHead::PositionInfo>& position);

    std::atomic<float>* rawParameter (const char* id) const;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    pluck::PresetManager presetManager;      // declared after apvts: it holds a reference to it
    juce::MidiKeyboardState keyboardState;   // shared with the editor's on-screen keyboard

    /** Beats per LFO cycle when synced: the division nearest the Rate knob. */
    float lfoSyncedBeats (float rateHz) const;

    std::atomic<double> hostBpm { 120.0 };

    pluck::PluckSynth synth;
    std::vector<pluck::KarplusVoice*> voices;   // owned by synth; kept to avoid dynamic_cast per block

    /** Atomic pointers into the parameter tree, resolved once in the constructor. */
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

    // Master chain state (mono; the string is mono and every output channel gets the same signal)
    juce::AudioBuffer<float> synthBuffer;    // mono sum of all voices
    juce::dsp::Oversampling<float> oversampling { 1, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    float driveDcIn1 = 0.0f, driveDcOut1 = 0.0f, driveDcCoeff = 0.999f;
    juce::SmoothedValue<float> driveSmoothed;
    juce::SmoothedValue<float> outputGainSmoothed;
    pluck::StereoWidener widener;                   // mono string -> wide stereo

    // Reverb, the last thing before the outputs, as a send: the dry signal
    // passes untouched and the room is added on top. The wet is worked out
    // beside the dry, so a knob at zero leaves the signal exactly as it was.
    juce::Reverb reverb;
    juce::AudioBuffer<float> reverbBuffer;
    juce::SmoothedValue<float> reverbSendSmoothed;
    bool reverbRunning = false;

    // Safety limiter: nothing leaves the plugin louder than +6 dBFS, and
    // nothing that is not a number leaves it at all.
    float limiterGain = 1.0f;
    float limiterRelease = 0.001f;

    // The two LFOs on the damper
    DamperLfo positionLfo, pressureLfo;
    float lfoSmoothingCoeff = 0.01f;
    float modWheel = 0.0f;                   // last CC1 seen, 0..1

    // Read-only telemetry for the editor
    std::atomic<float> stringLevel { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int>   activeVoiceCount { 0 };
    std::atomic<int>   noteOnCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordaAudioProcessor)
};
