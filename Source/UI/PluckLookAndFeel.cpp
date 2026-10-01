/*
  ==============================================================================

    PluckLookAndFeel.cpp

  ==============================================================================
*/

#include "PluckLookAndFeel.h"
#include "BinaryData.h"   // embedded title typeface

namespace pluck::ui
{

namespace
{
    constexpr float radius = 4.0f;

    /** Typefaces resolved once: embedded Bagnard for the title, the system
        UI face (SF Pro on macOS, not redistributable) for everything else. */
    struct Typefaces
    {
        Typefaces()
            : display (juce::Typeface::createSystemTypefaceFor (BinaryData::Bagnard_otf, BinaryData::Bagnard_otfSize)),
              regular (systemFace ("Regular")),
              medium  (systemFace ("Medium"))
        {
        }

        static juce::Typeface::Ptr systemFace (const juce::String& style)
        {
           #if JUCE_MAC
            // Private CoreText name for the system UI family, matched by style.
            juce::Font f (juce::FontOptions (".AppleSystemUIFont", style, 12.0f));
            if (auto face = f.getTypefacePtr(); face != nullptr && face->getName().isNotEmpty() && ! face->getName().contains ("Lucida"))
                return face;
           #endif
            if (auto face = juce::Typeface::findSystemTypeface())
                return face;
            return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), style, 12.0f)).getTypefacePtr();
        }

        juce::Typeface::Ptr display, regular, medium;
    };

    const Typefaces& typefaces()
    {
        static const Typefaces instance;
        return instance;
    }

    juce::Font font (const juce::Typeface::Ptr& typeface, float height)
    {
        return juce::Font (juce::FontOptions (typeface).withHeight (height));
    }
}

namespace fonts
{
    juce::Font title()   { return font (typefaces().display, 22.0f); }
    juce::Font name()    { return font (typefaces().medium,  12.5f); }
    juce::Font caption() { return font (typefaces().medium,  10.0f).withExtraKerningFactor (0.14f); }
    juce::Font value()   { return font (typefaces().regular, 11.0f); }
    juce::Font text()    { return font (typefaces().regular, 12.0f); }

    juce::String describe()
    {
        auto nameOf = [] (const juce::Typeface::Ptr& t) { return t != nullptr ? t->getName() + " " + t->getStyle() : juce::String ("(none)"); };
        return "title: " + nameOf (typefaces().display) + ", ui: " + nameOf (typefaces().regular) + " / " + nameOf (typefaces().medium);
    }
}

//==============================================================================
PluckLookAndFeel::PluckLookAndFeel()
{
    setDefaultSansSerifTypeface (typefaces().regular);

    setColour (juce::ResizableWindow::backgroundColourId, colours::white);
    setColour (juce::Label::textColourId,                 colours::ink);

    setColour (juce::ComboBox::backgroundColourId,        colours::field);
    setColour (juce::ComboBox::outlineColourId,           colours::hairline);
    setColour (juce::ComboBox::textColourId,              colours::ink);
    setColour (juce::ComboBox::arrowColourId,             colours::dim);
    setColour (juce::ComboBox::focusedOutlineColourId,    colours::mid);

    setColour (juce::PopupMenu::backgroundColourId,       colours::white);
    setColour (juce::PopupMenu::textColourId,             colours::ink);
    setColour (juce::PopupMenu::headerTextColourId,       colours::dim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::field);
    setColour (juce::PopupMenu::highlightedTextColourId,  colours::ink);

    setColour (juce::TextButton::buttonColourId,          colours::white);
    setColour (juce::TextButton::buttonOnColourId,        colours::white);
    setColour (juce::TextButton::textColourOffId,         colours::ink);
    setColour (juce::TextButton::textColourOnId,          colours::ink);

    setColour (juce::AlertWindow::backgroundColourId,     colours::white);
    setColour (juce::AlertWindow::textColourId,           colours::ink);
    setColour (juce::AlertWindow::outlineColourId,        colours::hairline);
    setColour (juce::TextEditor::backgroundColourId,      colours::field);
    setColour (juce::TextEditor::textColourId,            colours::ink);
    setColour (juce::TextEditor::outlineColourId,         colours::hairline);
    setColour (juce::TextEditor::focusedOutlineColourId,  colours::mid);
    setColour (juce::TextEditor::highlightColourId,       colours::track);
    setColour (juce::CaretComponent::caretColourId,       colours::ink);
}

//==============================================================================
void PluckLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    // Light disc with hairline rim, inner range track with the swept part in ink, ink pointer.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radiusPx = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto disc   = juce::Rectangle<float> (radiusPx * 2.0f, radiusPx * 2.0f).withCentre (centre);
    const bool hot    = slider.isMouseOverOrDragging() && slider.isEnabled();

    g.setColour (hot ? colours::fieldHover : colours::field);
    g.fillEllipse (disc);
    g.setColour (hot ? colours::mid : colours::hairline);
    g.drawEllipse (disc.reduced (0.5f), 1.0f);

    const auto arcRadius = radiusPx - 5.0f;
    const auto stroke = juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, stroke);

    if (sliderPos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (colours::ink);
        g.strokePath (value, stroke);
    }

    const auto tip   = centre.getPointOnCircumference (arcRadius - 5.0f, angle);
    const auto inner = centre.getPointOnCircumference (radiusPx * 0.38f, angle);
    g.setColour (colours::ink);
    g.drawLine ({ inner, tip }, 2.0f);
}

//==============================================================================
void PluckLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

    g.setColour (box.isMouseOver (true) ? colours::fieldHover : box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.hasKeyboardFocus (true) ? colours::mid : colours::hairline);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    const auto cx = bounds.getRight() - 14.0f;
    const auto cy = bounds.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (colours::dim);
    g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PluckLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (getComboBoxFont (box));
}

juce::Font PluckLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts::text();
}

//==============================================================================
void PluckLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const bool bordered = button.getProperties()[borderedProperty];
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool hot = (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown) && button.isEnabled();

    if (button.getProperties()[primaryProperty])
    {
        g.setColour (shouldDrawButtonAsDown ? colours::headerHover : (hot ? colours::headerField : colours::ink));
        g.fillRoundedRectangle (bounds, radius);
    }
    else if (bordered)
    {
        g.setColour (shouldDrawButtonAsDown ? colours::fieldHover : (hot ? colours::field : colours::white));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (button.isEnabled() ? colours::hairline : colours::field);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
    else if (hot)
    {
        const bool dark = button.getProperties()[darkProperty];
        g.setColour (dark ? colours::white.withAlpha (shouldDrawButtonAsDown ? 0.16f : 0.09f)
                          : (shouldDrawButtonAsDown ? colours::fieldHover : colours::field));
        g.fillRoundedRectangle (bounds, radius);
    }
}

void PluckLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    const bool primary = button.getProperties()[primaryProperty];
    g.setColour (! button.isEnabled() ? colours::mid
                                      : primary ? colours::white : button.findColour (juce::TextButton::textColourOffId));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

juce::Font PluckLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return fonts::text();
}

//==============================================================================
void PluckLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.fillAll (colours::white);
    g.setColour (colours::hairline);
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void PluckLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (colours::hairline);
        g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 8.0f, (float) area.getRight() - 8.0f);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (colours::field);
        g.fillRect (area.reduced (4, 0));
    }

    g.setFont (fonts::name());
    g.setColour (! isActive ? colours::mid : (isTicked ? colours::ink : colours::dim));
    g.drawText (text, area.reduced (16, 0), juce::Justification::centredLeft);

    if (isTicked)
    {
        // Dot marks the current preset
        g.setColour (colours::ink);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ (float) area.getX() + 8.0f, (float) area.getCentreY() }));
    }
}

juce::Font PluckLookAndFeel::getPopupMenuFont()
{
    return fonts::text();
}

void PluckLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                  int& idealWidth, int& idealHeight)
{
    idealHeight = isSeparator ? 9 : 26;
    idealWidth  = juce::GlyphArrangement::getStringWidthInt (fonts::name(), text) + 40;
}

//==============================================================================
// Dialogs: a white card with a hairline edge, the title in the display face.

void PluckLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& window,
                                     const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto bounds = window.getLocalBounds().toFloat();
    g.fillAll (colours::white);
    g.setColour (colours::hairline);
    g.drawRect (bounds, 1.0f);

    layout.draw (g, textArea.toFloat());
}

int PluckLookAndFeel::getAlertWindowButtonHeight()
{
    return 30;
}

void PluckLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), radius);
}

void PluckLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    g.setColour (focused ? colours::ink : colours::hairline);
    g.drawRoundedRectangle (juce::Rectangle<int> (width, height).toFloat().reduced (0.5f), radius, 1.0f);
}

juce::Font PluckLookAndFeel::getAlertWindowTitleFont()   { return fonts::title(); }
juce::Font PluckLookAndFeel::getAlertWindowMessageFont() { return fonts::name(); }
juce::Font PluckLookAndFeel::getAlertWindowFont()        { return fonts::name(); }

} // namespace pluck::ui
