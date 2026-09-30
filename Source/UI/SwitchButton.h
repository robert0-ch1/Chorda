/*
  ==============================================================================

    SwitchButton.h

    SyncButton is the small square that sits on the LFO rate knob and makes
    it follow the host's tempo. Its glyph is a quaver, drawn rather than set,
    so it needs no font with musical characters in it. It is a juce::Button
    in toggle mode, so a ButtonAttachment binds it to a parameter and host
    automation moves it.

    TargetSwitch is the Damp LFO's Target: a slide switch in a knob-sized
    square, Position at the top and Pressure at the bottom, that picks which
    damper LFO the knobs beside it edit. A small blue dot beside a name says
    that LFO is running.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

class SyncButton final : public juce::Button
{
public:
    explicit SyncButton (const juce::String& name) : juce::Button (name)
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (false);
        setTooltip ("Follow the host's tempo");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();

        g.setColour (on ? colours::ink : (highlighted ? colours::fieldHover : colours::white));
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (on ? colours::ink : colours::hairline);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        // A quaver: a filled head, a stem up its right side, one flag.
        const auto ink = on ? colours::white : colours::dim;
        const auto box = bounds.reduced (bounds.getWidth() * 0.26f, bounds.getHeight() * 0.2f);
        const auto headSize = box.getWidth() * 0.62f;
        const auto head = juce::Rectangle<float> (headSize, headSize * 0.82f)
                            .withCentre ({ box.getX() + headSize * 0.5f, box.getBottom() - headSize * 0.4f });

        g.setColour (ink);
        g.fillEllipse (head);

        const auto stemX = head.getRight() - 0.5f;
        g.drawLine (stemX, head.getCentreY(), stemX, box.getY(), 1.2f);

        juce::Path flag;
        flag.startNewSubPath (stemX, box.getY());
        flag.quadraticTo (box.getRight(), box.getY() + box.getHeight() * 0.18f,
                          stemX + box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.42f);
        g.strokePath (flag, juce::PathStrokeType (1.2f));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncButton)
};

//==============================================================================
class TargetSwitch final : public juce::Component,
                           private juce::Timer
{
public:
    /** @param names      the two choices, top first
        @param isRunning  tells whether each choice's LFO is on, for its dot */
    TargetSwitch (juce::StringArray names, std::function<bool (int)> isRunning)
        : choices (std::move (names)), running (std::move (isRunning))
    {
        setTitle (choices.joinIntoString (" / "));
        startTimerHz (30);   // the thumb's slide, and the running dots following the Amount knobs
    }

    ~TargetSwitch() override   { stopTimer(); }

    int  getSelected() const noexcept   { return selected; }
    std::function<void (int)> onChange;

    /** Its name above, like a knob's, then a square face the size of a knob:
        the first choice written at the top, the second at the bottom, and a
        short slide switch between them whose thumb goes to the one chosen. */
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const auto title = bounds.removeFromTop (titleHeight);
        g.setColour (colours::ink);
        g.setFont (fonts::name());
        g.drawText (name, title, juce::Justification::centred, false);

        const auto face = bounds.withSizeKeepingCentre (faceSize, faceSize);
        g.setColour (hovered ? colours::fieldHover : colours::field);
        g.fillRoundedRectangle (face, 6.0f);
        g.setColour (hovered ? colours::mid : colours::hairline);
        g.drawRoundedRectangle (face.reduced (0.5f), 6.0f, 1.0f);

        // The slot, and the thumb in it.
        const auto slot = trackBounds();
        g.setColour (colours::track);
        g.fillRoundedRectangle (slot, slot.getWidth() * 0.5f);

        const auto thumbSize = slot.getWidth() - 4.0f;
        const auto travel = slot.getHeight() - thumbSize - 4.0f;
        const auto thumb = juce::Rectangle<float> (thumbSize, thumbSize)
                             .withPosition (slot.getX() + 2.0f, slot.getY() + 2.0f + travel * position);
        g.setColour (colours::ink);
        g.fillEllipse (thumb);

        // The two names, spelled out, at the ends the thumb goes to, and a
        // dot beside each one that is running.
        const auto labelFont = fonts::value().withHeight (9.5f);
        for (int i = 0; i < 2; ++i)
        {
            const auto row = i == 0 ? face.withHeight (labelHeight).translated (0.0f, 2.0f)
                                    : face.withTrimmedTop (face.getHeight() - labelHeight).translated (0.0f, -2.0f);
            g.setColour (i == selected ? colours::ink : colours::mid);
            g.setFont (labelFont);
            g.drawText (choices[i], row, juce::Justification::centred, false);

            if (running && running (i))
            {
                const auto textWidth = juce::GlyphArrangement::getStringWidth (labelFont, choices[i]);
                g.setColour (colours::damper);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ row.getCentreX() - textWidth * 0.5f - 4.0f, row.getCentreY() }));
            }
        }
    }

    void mouseMove (const juce::MouseEvent&) override   { if (! hovered) { hovered = true;  repaint(); } }
    void mouseExit (const juce::MouseEvent&) override   { if (hovered)   { hovered = false; repaint(); } }

    // A click anywhere flips it, like a toggle; dragging the thumb moves it.
    void mouseDown (const juce::MouseEvent&) override   { dragged = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 3)
            return;
        dragged = true;
        choose (e.position.y > trackBounds().getCentreY() ? 1 : 0);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragged)
            choose (1 - selected);
    }

    static constexpr float titleHeight = 20.0f;   // the same as a knob's name
    static constexpr float faceSize    = 54.0f;   // the same as a knob
    static constexpr float labelHeight = 13.0f;

private:
    void choose (int choice)
    {
        if (choice == selected)
            return;
        selected = choice;
        repaint();
        if (onChange)
            onChange (selected);
    }

    juce::Rectangle<float> trackBounds() const
    {
        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromTop (titleHeight);
        const auto face = bounds.withSizeKeepingCentre (faceSize, faceSize);
        // A short slot between the two names.
        return juce::Rectangle<float> (12.0f, faceSize - 2.0f * labelHeight - 4.0f).withCentre (face.getCentre());
    }

    void timerCallback() override
    {
        // The thumb slides rather than jumps.
        const auto target = (float) selected;
        if (! juce::exactlyEqual (position, target))
        {
            position += (target - position) * 0.35f;
            if (std::abs (position - target) < 0.01f)
                position = target;
            repaint();
        }

        const auto state = running ? (running (0) ? 1 : 0) + (running (1) ? 2 : 0) : 0;
        if (state != shownRunning)
        {
            shownRunning = state;
            repaint();
        }
    }

    juce::StringArray choices;
    const juce::String name { "Target" };
    std::function<bool (int)> running;
    int   selected = 0, shownRunning = -1;
    float position = 0.0f;    ///< the thumb, 0 at the top, 1 at the bottom
    bool  hovered = false, dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TargetSwitch)
};

} // namespace pluck::ui
