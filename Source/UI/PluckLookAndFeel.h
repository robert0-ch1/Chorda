/*
  ==============================================================================

    PluckLookAndFeel.h

    Colour palette, type scale and the monochrome look-and-feel used by all
    widgets: white ground, hairline greys, ink for values and selection.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck::ui
{

namespace colours
{
    const juce::Colour white     { 0xffffffff };
    const juce::Colour field     { 0xfff4f4f4 };   ///< knob faces, fields
    const juce::Colour fieldHover{ 0xffebebeb };
    const juce::Colour hairline  { 0xffd2d2d2 };   ///< rules and borders
    const juce::Colour track     { 0xffdedede };   ///< knob range track
    const juce::Colour mid       { 0xff9a9a9a };   ///< tick marks, disabled text
    const juce::Colour dim       { 0xff6f6f6f };   ///< captions, values
    const juce::Colour ink       { 0xff161616 };   ///< text, string, pointers, selection fill

    // Dark header variants
    const juce::Colour headerField { 0xff2a2a2a };   ///< header fields and buttons
    const juce::Colour headerHover { 0xff383838 };
    const juce::Colour headerLine  { 0xff454545 };   ///< header rules and borders
    const juce::Colour headerDim   { 0xff8a8a8a };   ///< disabled header icons

    const juce::Colour damper { 0xff4a90e2 };   ///< damper marker

    // Inverted string panel
    const juce::Colour panel     { 0xff17181a };   ///< background
    const juce::Colour panelInk  { 0xfff4f4f4 };   ///< string and markers
    const juce::Colour panelDim  { 0xff7c8088 };   ///< captions, damper stalk
    const juce::Colour panelLine { 0xff3c4046 };   ///< grid lines, midpoint tick
}

/** Type scale used for all text. */
namespace fonts
{
    juce::Font title();     ///< window title
    juce::Font name();      ///< control names, buttons, fields
    juce::Font caption();   ///< group captions, small tracked capitals
    juce::Font value();     ///< values shown on hover
    juce::Font text();      ///< free text: preset names, menus, dialogs
    juce::String describe();   ///< resolved typeface names, for diagnostics
}

//==============================================================================
class PluckLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PluckLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    /** TextButton property keys: hairline border, and dark-header hover style. */
    static constexpr auto borderedProperty = "bordered";
    static constexpr auto darkProperty     = "dark";
};

} // namespace pluck::ui
