/*
  ==============================================================================

    ParameterKnob.h

    A rotary control with its name above. While you hover or drag, the value
    takes the name's place, as text or, for a knob that has one, as a small
    drawing. Double-click resets to the default, drag or
    mouse-wheel to change.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID,
                   const juce::String& title)
        : name (title)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setTitle (title);
        slider.setWantsKeyboardFocus (false);
        slider.onValueChange = [this] { if (showingValue) showValue(); };
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);

        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, parameterID, slider);
        showName();
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        label.setBounds (bounds.removeFromTop (titleHeight));
        const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
        slider.setBounds (bounds.withSizeKeepingCentre (side, side));
    }

    /** Lets the value read as something the parameter itself cannot know,
        such as the note division a synced rate has landed on. */
    void setValueTextSource (std::function<juce::String()> source)
    {
        valueTextSource = std::move (source);
    }

    /** Draws the value instead of writing it: the Tone knob shows the
        waveform it makes. Called with the label's area and the value. */
    void setValueDrawing (std::function<void (juce::Graphics&, juce::Rectangle<float>, float)> drawing)
    {
        valueDrawing = std::move (drawing);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (showingValue && valueDrawing)
            valueDrawing (g, label.getBounds().toFloat(), (float) slider.getValue());
    }

    void mouseEnter (const juce::MouseEvent&) override   { showValue(); }
    void mouseExit  (const juce::MouseEvent&) override   { if (! slider.isMouseButtonDown()) showName(); }
    void mouseUp    (const juce::MouseEvent&) override   { if (! slider.isMouseOver()) showName(); }

    static constexpr int titleHeight = 20;

private:
    void showName()
    {
        showingValue = false;
        label.setFont (fonts::name());
        label.setColour (juce::Label::textColourId, colours::ink);
        label.setText (name, juce::dontSendNotification);
        repaint();
    }

    void showValue()
    {
        showingValue = true;
        label.setFont (fonts::value());
        label.setColour (juce::Label::textColourId, colours::dim);
        label.setText (valueDrawing ? juce::String()
                                    : (valueTextSource ? valueTextSource() : slider.getTextFromValue (slider.getValue())),
                       juce::dontSendNotification);
        repaint();
    }

    juce::Slider slider;
    juce::Label label;
    juce::String name;
    std::function<juce::String()> valueTextSource;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, float)> valueDrawing;
    bool showingValue = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterKnob)
};

} // namespace pluck::ui
