/*
  ==============================================================================

    ParameterBox.h

    A parameter as a line of text: its name, then its value, sitting on the
    dark string panel with no box around it. Drag up or down to change it,
    double-click to put it back to its default, or roll the wheel over it. A
    choice (the voice mode) also opens its list on a plain click.

    Used for the few controls that belong in a line of text rather than in a
    row of knobs: Voices, Glide and the octave transposer.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

class ParameterBox final : public juce::Component
{
public:
    /** @param displayName    the name drawn before the value
        @param justification  where the text sits in the component: the voice
                              line puts one at each end and one in the middle */
    ParameterBox (juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID,
                  const juce::String& displayName, juce::Justification justification)
        : parameter (*apvts.getParameter (parameterID)),
          attachment (parameter, [this] (float v) { value = v; repaint(); }),
          name (displayName),
          placement (justification)
    {
        setTitle (parameter.getName (32));
        setMouseCursor (isChoice() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto nameFont  = fonts::name();
        const auto valueFont = fonts::text().withHeight (nameFont.getHeight());
        const auto valueText = parameter.getCurrentValueAsText();

        const auto nameWidth  = juce::GlyphArrangement::getStringWidth (nameFont, name);
        const auto valueWidth = juce::GlyphArrangement::getStringWidth (valueFont, valueText);
        const auto total      = nameWidth + nameGap + valueWidth;

        const auto bounds = getLocalBounds().toFloat();
        auto x = bounds.getX() + valuePad;
        if (placement.testFlags (juce::Justification::horizontallyCentred))
            x = bounds.getCentreX() - total * 0.5f;
        else if (placement.testFlags (juce::Justification::right))
            x = bounds.getRight() - valuePad - total;

        // The value is what you move, so it is what lights up.
        const auto valueArea = juce::Rectangle<float> (x + nameWidth + nameGap, bounds.getY(), valueWidth, bounds.getHeight());
        if (hovering || dragging)
        {
            g.setColour (colours::panelInk.withAlpha (dragging ? 0.16f : 0.09f));
            g.fillRoundedRectangle (valueArea.expanded (valuePad, 0.0f).reduced (0.0f, 2.0f), 4.0f);
        }

        g.setColour (colours::panelInk);
        g.setFont (nameFont);
        g.drawText (name, juce::Rectangle<float> (x, bounds.getY(), nameWidth + 1.0f, bounds.getHeight()),
                    juce::Justification::centredLeft, false);
        g.setFont (valueFont);
        g.drawText (valueText, valueArea.withWidth (valueWidth + 1.0f), juce::Justification::centredLeft, false);
    }

    void mouseEnter (const juce::MouseEvent&) override   { hovering = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override   { hovering = false; repaint(); }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = true;
        dragStart = parameter.getValue();
        attachment.beginGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (
                juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / dragPixels)));   // up is more
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        dragging = false;
        attachment.endGesture();
        repaint();

        if (isChoice() && ! e.mouseWasDraggedSinceMouseDown())
            showChoices();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (parameter.getDefaultValue()));
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs (wheel.deltaY) < 1.0e-4f)
            return;

        // A choice steps one entry per notch; anything else moves by a fixed share of its range.
        const auto step = isChoice() ? 1.0f / (float) juce::jmax (1, parameter.getNumSteps() - 1) : wheelStep;
        attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (
            juce::jlimit (0.0f, 1.0f, parameter.getValue() + (wheel.deltaY > 0.0f ? step : -step))));
    }

private:
    bool isChoice() const   { return dynamic_cast<juce::AudioParameterChoice*> (&parameter) != nullptr; }

    void showChoices()
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter);
        if (choice == nullptr)
            return;

        juce::PopupMenu menu;
        for (int i = 0; i < choice->choices.size(); ++i)
            menu.addItem (i + 1, choice->choices[i], true, i == choice->getIndex());

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe = juce::Component::SafePointer<ParameterBox> (this)] (int result)
                            {
                                if (safe != nullptr && result > 0)
                                    safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                            });
    }

    static constexpr float dragPixels = 180.0f;   ///< a full sweep of the range
    static constexpr float wheelStep  = 0.04f;
    static constexpr float nameGap    = 6.0f;
    static constexpr float valuePad   = 4.0f;

    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    juce::String name;
    juce::Justification placement;
    float value     = 0.0f;
    float dragStart = 0.0f;
    bool  hovering  = false;
    bool  dragging  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterBox)
};

} // namespace pluck::ui
