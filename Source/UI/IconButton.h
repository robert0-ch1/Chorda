/*
  ==============================================================================

    IconButton.h

    Square bordered button with a drawn line icon (save, save as, delete).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

class IconButton final : public juce::Button
{
public:
    enum class Icon { save, saveAs, trash };

    /** @param onDark  use the dark header style */
    IconButton (const juce::String& name, Icon iconToShow, bool onDark = false)
        : juce::Button (name), icon (iconToShow), dark (onDark)
    {
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool hot = (highlighted || down) && isEnabled();

        if (dark)
        {
            g.setColour (hot ? colours::headerHover : colours::headerField);
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (colours::headerLine);
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }
        else
        {
            g.setColour (down ? colours::fieldHover : (hot ? colours::field : colours::white));
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (isEnabled() ? colours::hairline : colours::field);
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }

        // 14 x 14 icon box, centred
        const auto box = juce::Rectangle<float> (14.0f, 14.0f).withCentre (bounds.getCentre());
        const auto inkColour = dark ? (isEnabled() ? colours::white : colours::headerDim)
                                    : (isEnabled() ? colours::ink   : colours::mid);
        drawIcon (g, icon, box, inkColour, dark ? colours::headerField : colours::white);
    }

private:
    static void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour ink, juce::Colour paper)
    {
        g.setColour (ink);
        const juce::PathStrokeType stroke (1.3f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
        const auto x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();

        switch (icon)
        {
            case Icon::save:
            case Icon::saveAs:
            {
                // Floppy disk: clipped corner, label slot, shutter.
                juce::Path disk;
                disk.startNewSubPath (x, y);
                disk.lineTo (x + w - 3.5f, y);
                disk.lineTo (x + w, y + 3.5f);
                disk.lineTo (x + w, y + h);
                disk.lineTo (x, y + h);
                disk.closeSubPath();
                g.strokePath (disk, stroke);

                g.drawRect (juce::Rectangle<float> (x + 3.5f, y, w - 7.0f, 4.5f), 1.3f);
                g.drawRect (juce::Rectangle<float> (x + 3.0f, y + h - 5.5f, w - 6.0f, 5.5f), 1.3f);

                if (icon == Icon::saveAs)
                {
                    // Plus badge
                    const auto badge = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x + w - 1.0f, y + h - 1.0f });
                    g.setColour (paper);
                    g.fillEllipse (badge.expanded (1.5f));
                    g.setColour (ink);
                    g.drawLine (badge.getX(), badge.getCentreY(), badge.getRight(), badge.getCentreY(), 1.6f);
                    g.drawLine (badge.getCentreX(), badge.getY(), badge.getCentreX(), badge.getBottom(), 1.6f);
                }
                break;
            }

            case Icon::trash:
            {
                // Bin: lid, handle, tapered body, two slots
                g.drawLine (x, y + 2.5f, x + w, y + 2.5f, 1.3f);
                g.drawLine (x + w * 0.35f, y + 0.6f, x + w * 0.65f, y + 0.6f, 1.3f);

                juce::Path body;
                body.startNewSubPath (x + 1.5f, y + 2.5f);
                body.lineTo (x + w - 1.5f, y + 2.5f);
                body.lineTo (x + w - 2.5f, y + h);
                body.lineTo (x + 2.5f, y + h);
                body.closeSubPath();
                g.strokePath (body, stroke);

                g.drawLine (x + w * 0.38f, y + 5.5f, x + w * 0.38f, y + h - 3.0f, 1.1f);
                g.drawLine (x + w * 0.62f, y + 5.5f, x + w * 0.62f, y + h - 3.0f, 1.1f);
                break;
            }
        }
    }

    Icon icon;
    bool dark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};

} // namespace pluck::ui
