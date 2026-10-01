/*
  ==============================================================================

    PluginEditor.h

    Plugin window: a header with title and preset bar above a two-column grid
    of cards. MainView is laid out at a fixed reference size and the editor
    scales it to the window, keeping the aspect ratio.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/PluckLookAndFeel.h"
#include "UI/SectionCard.h"
#include "UI/ParameterKnob.h"
#include "UI/ParameterBox.h"
#include "UI/SwitchButton.h"
#include "UI/StringView.h"
#include "UI/PresetBar.h"

//==============================================================================
/** Everything visible, laid out at MainView::width x MainView::height. */
class MainView final : public juce::Component
{
public:
    explicit MainView (ChordaAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width  = 600;
    static constexpr int height = 574;

private:
    using Knob = pluck::ui::ParameterKnob;
    using Card = pluck::ui::SectionCard;

    /** Draws the Exciter knob's value as the waveform it makes, in place of its name. */
    static void drawTone (juce::Graphics&, juce::Rectangle<float> area, float tone);

    /** Spreads knobs evenly across an area, each centred in its column. */
    static void layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);

    juce::AudioProcessorValueTreeState& apvts;

    pluck::ui::PresetBar presetBar;

    Card stringCard   { {}, true };   // inverted panel, headed by the voice row
    Card timbreCard   { "TIMBRE" };
    Card lfoCard      { "DAMP LFO" };
    Card envelopeCard { "ENVELOPE" };
    Card outputCard   { "OUTPUT" };

    Knob toneKnob, brightnessKnob, subKnob;

    // Voice row across the top of the string panel
    pluck::ui::ParameterBox voiceModeBox, glideBox, octaveBox;

    pluck::ui::StringView stringView;

    Knob attackKnob, decayKnob, sustainKnob, releaseKnob;

    // Position and pressure LFOs share one knob slot; the switch selects which pair is visible.
    Knob lfoAmountKnob, lfoRateKnob, lfoPressureAmountKnob, lfoPressureRateKnob;
    pluck::ui::TargetSwitch lfoTargetSwitch;
    void showLfo (int target);
    pluck::ui::SyncButton lfoSyncButton { "Position LFO Sync" }, lfoPressureSyncButton { "Pressure LFO Sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lfoSyncAttachment, lfoPressureSyncAttachment;

    Knob widthKnob, driveKnob, reverbKnob, gainKnob;

    juce::Rectangle<int> header;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

//==============================================================================
class ChordaAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ChordaAudioProcessorEditor (ChordaAudioProcessor&);
    ~ChordaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    pluck::ui::PluckLookAndFeel lookAndFeel;
    MainView mainView;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordaAudioProcessorEditor)
};
