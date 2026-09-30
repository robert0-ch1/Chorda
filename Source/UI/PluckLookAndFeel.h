/*
  ==============================================================================

    PluckLookAndFeel.h

    Monochrome, minimal drawing for every widget: white ground, hairline
    greys, black reserved for the string, knob pointers and selected states.
    Typeface: Space Grotesk (embedded, SIL Open Font License), so the plugin
    looks the same in every host and on every platform.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace pluck::ui
{

namespace colours
{
    const juce::Colour white     { 0xffffffff };
    const juce::Colour field     { 0xfff4f4f4 };   ///< panels, knob faces, fields
    const juce::Colour fieldHover{ 0xffebebeb };
    const juce::Colour hairline  { 0xffd2d2d2 };   ///< every rule and border
    const juce::Colour track     { 0xffdedede };   ///< knob range track
    const juce::Colour mid       { 0xff9a9a9a };   ///< tick marks, disabled text
    const juce::Colour dim       { 0xff6f6f6f };   ///< captions, values
    const juce::Colour ink       { 0xff161616 };   ///< text, string, pointers, selection fill

    // The header bar is ink with white text and icons
    const juce::Colour headerField { 0xff2a2a2a };   ///< fields and buttons on the header
    const juce::Colour headerHover { 0xff383838 };
    const juce::Colour headerLine  { 0xff454545 };   ///< rules and borders on the header
    const juce::Colour headerDim   { 0xff8a8a8a };   ///< disabled icons on the header

    const juce::Colour damper { 0xff4a90e2 };   ///< the damper: a blue dot

    // The string is drawn on an inverted panel, so it has its own few colours
    const juce::Colour panel     { 0xff17181a };   ///< the panel itself
    const juce::Colour panelInk  { 0xfff4f4f4 };   ///< the string and its markers
    const juce::Colour panelDim  { 0xff7c8088 };   ///< the caption, the damper stalk
    const juce::Colour panelLine { 0xff3c4046 };   ///< the half-way tick
}

/** The type scale. All text goes through these. */
namespace fonts
{
    juce::Font title();     ///< window title
    juce::Font name();      ///< control names, buttons, fields
    juce::Font caption();   ///< group captions, small tracked capitals
    juce::Font value();     ///< values shown on hover
    juce::Font text();      ///< free text: preset names, menus, dialogs
    juce::String describe();   ///< which typefaces were resolved, for diagnostics
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

    /** Property key: set to true on a TextButton to draw it with a hairline border. */
    static constexpr auto borderedProperty = "bordered";
    static constexpr auto darkProperty     = "dark";       ///< a text button on the dark header
};

} // namespace pluck::ui
