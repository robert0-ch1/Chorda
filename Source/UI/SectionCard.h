/*
  ==============================================================================

    SectionCard.h

    A light card with a centred small-capitals caption, or none when the
    caption is empty. Each group of controls lives on one of these, so the
    groups read as separate objects. Children are added by the editor;
    getContentBounds() is the area below the caption.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluckLookAndFeel.h"

namespace pluck::ui
{

class SectionCard final : public juce::Component
{
public:
    /** @param darkPanel  true for the inverted panel the string is drawn on */
    explicit SectionCard (juce::String captionText, bool darkPanel = false)
        : caption (std::move (captionText)), dark (darkPanel)
    {
        setInterceptsMouseClicks (false, true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (dark ? colours::panel : colours::field);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (dark ? colours::panel : colours::hairline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        if (caption.isNotEmpty())
        {
            g.setColour (dark ? colours::panelDim : colours::dim);
            g.setFont (fonts::caption());
            g.drawText (caption, getLocalBounds().withHeight (captionHeight).withTrimmedTop (padding), juce::Justification::centred);
        }
    }

    /** The area below the caption (if any), inset from the edges. */
    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (getCaptionSpace());
    }

    int getCaptionSpace() const noexcept   { return caption.isEmpty() ? 0 : captionHeight; }

    static constexpr int captionHeight = 22;
    static constexpr int padding       = 10;

private:
    juce::String caption;
    bool dark = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionCard)
};

} // namespace pluck::ui
