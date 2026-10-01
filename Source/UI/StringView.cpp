/*
  ==============================================================================

    StringView.cpp

  ==============================================================================
*/

#include "StringView.h"
#include "PluckLookAndFeel.h"
#include "../Parameters.h"

namespace pluck::ui
{

namespace
{
    constexpr float endRadius     = 5.5f;
    constexpr float markerSize    = 14.0f;
    constexpr float maxAmplitude  = 30.0f;   ///< pixels at full level, deliberately exaggerated
    constexpr int   numSnapshots  = 7;       ///< faint earlier phases drawn behind the string
    constexpr float levelDecay    = 0.93f;   ///< per-frame release of the display level
    constexpr float periodsPerFrame = 0.083f; ///< about 5 cycles per second at 60 Hz
    constexpr float secondsPerFrame = 1.0f / 60.0f;
    constexpr int   numPoints     = 160;
    constexpr int   numPartials   = 24;
    constexpr float pressurePixels = 110.0f; ///< vertical drag distance for the full pressure range
    constexpr float damperLiftLight = 82.0f;  ///< dot height above the string at zero pressure
    constexpr float damperLiftHard  = 14.0f;  ///< dot height at full pressure
    constexpr float handAspect      = 100.0f / 130.0f;   ///< fallback hand width / height
    constexpr float handHeight      = markerSize * 2.9f * 1.2f;
    constexpr float handDrop        = -1.5f;  ///< fingertip offset from the string; negative overlaps it
    constexpr float pressPixels     = 7.0f;   ///< string deflection under a held pick

    // Pluck animation: three frames, 0.1 s each.
    constexpr int   pluckFrames       = 3;
    constexpr float pluckFrameSeconds = 0.1f;
    constexpr float pluckRise         = 4.0f;              ///< hand lift during the pluck
    constexpr float handFrameScale    = 3.0f;              ///< frames cached at 3x draw size

    const juce::Colour damperColour = colours::damper;
}

/** Pluck hand frames, sharing one crop, and the fingertip position. */
struct StringView::HandIcon
{
    std::array<juce::Image, pluckFrames> frames;   ///< frame 0 doubles as the rest pose
    float aspect  = handAspect;   ///< width over height
    float fingerX = 0.38f;        ///< fingertip x, 0..1 of width
};

//==============================================================================
StringView::StringView (juce::AudioProcessorValueTreeState& apvts, std::function<float()> source,
                        std::function<float()> modulationSource, std::function<float()> pressureSource,
                        std::function<int()> noteOns)
    : brightnessParameter (*apvts.getParameter (ParamID::stringBrightness)),
      levelSource (std::move (source)),
      damperModulationSource (std::move (modulationSource)),
      pressureModulationSource (std::move (pressureSource)),
      noteOnSource (std::move (noteOns))
{
    setTitle ("String");

    auto onChange = [this] { repaint(); };
    pickPosition   = std::make_unique<Bound> (*apvts.getParameter (ParamID::exciterPosition),      onChange);
    damperPosition = std::make_unique<Bound> (*apvts.getParameter (ParamID::stringDamper),         onChange);
    damperPressure = std::make_unique<Bound> (*apvts.getParameter (ParamID::stringDamperPressure), onChange);
    positionLfoAmount = std::make_unique<Bound> (*apvts.getParameter (ParamID::lfoAmount),         onChange);
    pressureLfoAmount = std::make_unique<Bound> (*apvts.getParameter (ParamID::lfoPressureAmount), onChange);

    // Ignore note-ons that happened before the editor opened.
    if (noteOnSource)
        lastNoteOnCount = noteOnSource();

    // Seed from the current level so offscreen renders show a moving string.
    level = displayLevelFor (levelSource());
    time  = 0.06f;
    age   = 0.08f;

    startTimerHz (60);
}

StringView::~StringView()
{
    stopTimer();
}

//==============================================================================
float StringView::bridgeX() const noexcept   { return (float) postInset; }
float StringView::nutX() const noexcept      { return (float) (getWidth() - postInset); }
float StringView::stringY() const noexcept
{
    // Anchored so the hand below and the raised damper above stay inside the bounds.
    return (float) getHeight() - (handDrop + handHeight + 3.0f);
}

float StringView::xForPosition (float position) const noexcept
{
    return bridgeX() + position * (nutX() - bridgeX());
}

float StringView::positionForX (float x) const noexcept
{
    return juce::jlimit (0.0f, 1.0f, (x - bridgeX()) / (nutX() - bridgeX()));
}

float StringView::movingDamperPosition() const noexcept
{
    // Used for the string pinch. Returns 0 when the damper is off, whatever the LFO does.
    if (mirroredPosition (damperPosition->value) <= 0.0005f
        || (damperPressure->value <= 0.0f && pressureLfoAmount->value <= 0.0f))
        return 0.0f;
    if (damperModulated >= 0.0f)
        return juce::jlimit (pickPositionMinimum, positionMaximum, damperModulated);
    return damperPosition->value;
}

juce::Rectangle<float> StringView::markerBounds (int index) const noexcept
{
    const auto y = stringY();
    const auto size = markerSize;

    if (index == pick)
    {
        // Hand placed so the fingertip (from the icon, if loaded) sits at the pick x.
        const auto x = xForPosition (pickPosition->value);
        const auto* icon = handIcon();
        const auto aspect  = icon != nullptr ? icon->aspect  : handAspect;
        const auto fingerX = icon != nullptr ? icon->fingerX : 0.38f;
        const auto h = handHeight, w = h * aspect;
        const auto rise = pluckFrame() >= 0 ? pluckRise : 0.0f;
        return { x - w * fingerX, y + handDrop - rise, w, h };
    }

    // Dot height maps inversely to pressure.
    const auto x = xForPosition (shownDamperPosition());
    const auto lift = juce::jmap (shownDamperPressure(), 0.0f, 1.0f, damperLiftLight, damperLiftHard);
    return { x - size * 0.5f, y - lift - size, size, size };
}

bool StringView::damperFrozen() const noexcept
{
    return auraHovered || hoveredMarker == damper || draggedMarker == damper;
}

float StringView::shownDamperPosition() const noexcept
{
    // Unlike movingDamperPosition, an off damper keeps its set position so the dot stays grabbable.
    if (damperFrozen() || damperModulated < 0.0f || mirroredPosition (damperPosition->value) <= 0.0005f)
        return damperPosition->value;
    return juce::jlimit (pickPositionMinimum, positionMaximum, damperModulated);
}

float StringView::shownDamperPressure() const noexcept
{
    if (damperFrozen() || pressureModulated < 0.0f)
        return damperPressure->value;
    return juce::jlimit (0.0f, 1.0f, pressureModulated);
}

juce::Rectangle<float> StringView::auraBounds() const noexcept
{
    const bool moving  = positionLfoAmount->value > 0.0f;
    const bool pressing = pressureLfoAmount->value > 0.0f;
    if (mirroredPosition (damperPosition->value) <= 0.0005f
        || (damperPressure->value <= 0.0f && ! pressing) || ! (moving || pressing))
        return {};

    // Horizontal: position LFO swing. Vertical: from the lightest modulated pressure down to the string.
    const auto centre = damperPosition->value;
    const auto swing  = positionLfoAmount->value * lfoPositionDepth;
    const auto left   = xForPosition (juce::jlimit (pickPositionMinimum, positionMaximum, centre - swing)) - markerSize * 0.5f;
    const auto right  = xForPosition (juce::jlimit (pickPositionMinimum, positionMaximum, centre + swing)) + markerSize * 0.5f;

    const auto lightest = juce::jlimit (0.0f, 1.0f, damperPressure->value - pressureLfoAmount->value * lfoPressureDepth);
    const auto top = stringY() - juce::jmap (lightest, 0.0f, 1.0f, damperLiftLight, damperLiftHard) - markerSize;

    return juce::Rectangle<float>::leftTopRightBottom (left, top, right, stringY() + 6.0f);
}

void StringView::drawAura (juce::Graphics& g) const
{
    const auto area = auraBounds();
    if (area.isEmpty())
        return;

    // Soft pulsing glow with drifting sparks; brighter while the dot is frozen.
    const auto breath   = 0.5f + 0.5f * std::sin (auraPhase);
    const auto strength = (damperFrozen() ? 1.0f : 0.7f) * (0.8f + 0.2f * breath);
    const auto centre   = area.getCentre();

    // Wide blurred bloom
    juce::Path bloom;
    bloom.addEllipse (area.reduced (area.getWidth() * 0.2f, area.getHeight() * 0.25f));
    juce::DropShadow (damperColour.withAlpha (0.35f * strength), 30, {}).drawForPath (g, bloom);

    // Radial falloff stretched to the area
    {
        juce::Graphics::ScopedSaveState state (g);
        const auto rx = area.getWidth() * 0.66f, ry = area.getHeight() * 0.8f;
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, centre.x, centre.y));
        juce::ColourGradient haze (damperColour.withAlpha (0.48f * strength), centre.x, centre.y,
                                   damperColour.withAlpha (0.0f), centre.x + rx, centre.y, true);
        haze.addColour (0.5, damperColour.withAlpha (0.24f * strength));
        g.setGradientFill (haze);
        g.fillEllipse (juce::Rectangle<float> (rx * 2.0f, rx * 2.0f).withCentre (centre));
    }

    // Highlight along the string segment covered
    juce::ColourGradient thread (damperColour.withAlpha (0.0f), area.getX(), stringY(),
                                 damperColour.withAlpha (0.0f), area.getRight(), stringY(), false);
    thread.addColour (0.5, damperColour.brighter (0.4f).withAlpha (0.35f * strength));
    g.setGradientFill (thread);
    g.fillRoundedRectangle (juce::Rectangle<float> (area.getX(), stringY() - 1.5f, area.getWidth(), 3.0f), 1.5f);

    // Sparks at hash-derived positions, drifting up and twinkling at different rates.
    constexpr int sparks = 9;
    for (int i = 0; i < sparks; ++i)
    {
        const auto seed = (float) i * 12.9898f;
        const auto u = std::fmod (std::abs (std::sin (seed) * 43758.5453f), 1.0f);
        const auto v = std::fmod (std::abs (std::sin (seed * 1.7f) * 24634.6345f), 1.0f);
        const auto twinkle = 0.5f + 0.5f * std::sin (auraPhase * (1.0f + (float) (i % 3)) + seed);
        const auto drift = std::fmod (v + auraPhase / juce::MathConstants<float>::twoPi * 0.3f, 1.0f);

        const auto p = juce::Point<float> (area.getX() + area.getWidth() * (0.1f + 0.8f * u),
                                           area.getBottom() - area.getHeight() * drift);
        const auto r = 0.8f + 1.2f * twinkle;
        g.setColour (colours::panelInk.interpolatedWith (damperColour, 0.5f).withAlpha (0.55f * twinkle * strength));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p));
    }
}

int StringView::markerAt (juce::Point<int> p) const noexcept
{
    for (int i = numMarkers - 1; i >= 0; --i)
        if (markerBounds (i).expanded (6.0f).contains (p.toFloat()))
            return i;
    return -1;
}

float StringView::displacementAt (float x, float phaseOffset) const noexcept
{
    // Fourier series of a triangle with its apex at the pick. Partial n decays
    // with n^2, faster at low Brightness, so the shape relaxes to the fundamental.
    const auto p = juce::jlimit (0.03f, 0.97f, pickPosition->value);
    const auto brightness = brightnessParameter.getValue();            // 0..1, logarithmic
    // Kept slow so the kink at the pick stays visible.
    const auto dampingRate = juce::jmap (1.0f - brightness, 0.0f, 1.0f, 1.2f, 6.0f);   // per second, scaled by n^2

    // Normalises the series to unit height at the apex.
    const auto norm = 2.0f / (juce::MathConstants<float>::pi * juce::MathConstants<float>::pi * p * (1.0f - p));

    float shape = 0.0f;
    for (int n = 1; n <= numPartials; ++n)
    {
        const auto fn = (float) n;
        const auto amplitude = norm * std::sin (fn * juce::MathConstants<float>::pi * p) / (fn * fn);
        const auto damping   = std::exp (-dampingRate * age * (fn * fn - 1.0f) / (float) numPartials);
        shape += amplitude * damping * std::sin (fn * juce::MathConstants<float>::pi * x)
               * std::cos (juce::MathConstants<float>::twoPi * fn * (time + phaseOffset));
    }

    // Damper pinches the shape towards a node, scaled by pressure.
    if (movingDamperPosition() > 0.0005f)
    {
        const auto d = movingDamperPosition();
        const auto pinch = damperPressure->value * std::exp (-std::pow ((x - d) / 0.06f, 2.0f));
        shape *= 1.0f - pinch;
    }

    // Held pick adds a static triangular deflection with its apex at the pick.
    const auto give = press * pressPixels * (x < p ? x / p : (1.0f - x) / (1.0f - p));

    return shape * level * maxAmplitude + give;
}

//==============================================================================

namespace
{
    /** Ink coverage of a pixel, 0..1: opacity times darkness. */
    float inkAt (const juce::Image::BitmapData& pixels, int x, int y)
    {
        const auto c = pixels.getPixelColour (x, y);
        const auto alpha = c.getFloatAlpha();
        if (alpha <= 0.0f)
            return 0.0f;
        return alpha * (1.0f - c.withAlpha (1.0f).getPerceivedBrightness());
    }
}

const StringView::HandIcon* StringView::handIcon()
{
    static const std::unique_ptr<HandIcon> icon = []() -> std::unique_ptr<HandIcon>
    {
        int size = 0;

        // Loaded once from three line-art PNGs. Light pixels flood-filled from the
        // edges become transparent; the rest is recoloured for the panel. All
        // frames share one crop so the hand does not jump, and are downscaled once.
        {
            std::array<juce::Image, pluckFrames> sources;
            bool found = true;
            for (int f = 0; f < pluckFrames && found; ++f)
            {
                const juce::String name = "hand_pluck_" + juce::String (f + 1) + "_png";
                if (const auto* data = BinaryData::getNamedResource (name.toRawUTF8(), size))
                    sources[(size_t) f] = juce::ImageFileFormat::loadFrom (data, (size_t) size);
                found = sources[(size_t) f].isValid();
            }

            if (found)
            {
                const auto w = sources[0].getWidth(), h = sources[0].getHeight();
                std::array<std::vector<uint8_t>, pluckFrames> outside;   // 1 = background
                juce::Rectangle<int> bounds;
                float fingerSum = 0.0f;
                int fingerCount = 0;

                for (int f = 0; f < pluckFrames; ++f)
                {
                    const juce::Image::BitmapData pixels (sources[(size_t) f], juce::Image::BitmapData::readOnly);
                    auto& mask = outside[(size_t) f];
                    mask.assign ((size_t) (w * h), 0);
                    auto light = [&] (int x, int y) { return inkAt (pixels, x, y) < 0.5f; };

                    std::vector<int> stack;
                    auto push = [&] (int x, int y)
                    {
                        const auto i = y * w + x;
                        if (mask[(size_t) i] == 0 && light (x, y)) { mask[(size_t) i] = 1; stack.push_back (i); }
                    };
                    for (int x = 0; x < w; ++x) { push (x, 0); push (x, h - 1); }
                    for (int y = 0; y < h; ++y) { push (0, y); push (w - 1, y); }
                    while (! stack.empty())
                    {
                        const auto i = stack.back();
                        stack.pop_back();
                        const auto x = i % w, y = i / w;
                        if (x > 0) push (x - 1, y);
                        if (x < w - 1) push (x + 1, y);
                        if (y > 0) push (x, y - 1);
                        if (y < h - 1) push (x, y + 1);
                    }

                    int topRow = -1;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                            if (mask[(size_t) (y * w + x)] == 0)
                            {
                                bounds = bounds.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1) : bounds.getUnion ({ x, y, 1, 1 });
                                if (f == 0)
                                {
                                    if (topRow < 0) topRow = y;
                                    if (y < topRow + 12) { fingerSum += (float) x; ++fingerCount; }
                                }
                            }
                }

                if (! bounds.isEmpty())
                {
                    auto result = std::make_unique<HandIcon>();
                    const auto targetHeight = juce::roundToInt (handHeight * handFrameScale);
                    const auto targetWidth  = juce::roundToInt ((float) targetHeight * (float) bounds.getWidth() / (float) bounds.getHeight());

                    for (int f = 0; f < pluckFrames; ++f)
                    {
                        juce::Image cut (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
                        {
                            const juce::Image::BitmapData in (sources[(size_t) f], juce::Image::BitmapData::readOnly);
                            juce::Image::BitmapData out (cut, juce::Image::BitmapData::writeOnly);
                            for (int y = 0; y < bounds.getHeight(); ++y)
                                for (int x = 0; x < bounds.getWidth(); ++x)
                                {
                                    const auto sx = x + bounds.getX(), sy = y + bounds.getY();
                                    if (outside[(size_t) f][(size_t) (sy * w + sx)] != 0)
                                        continue;
                                    out.setPixelColour (x, y, colours::panelInk.interpolatedWith (colours::panel, inkAt (in, sx, sy)));
                                }
                        }
                        result->frames[(size_t) f] = cut.rescaled (targetWidth, targetHeight, juce::Graphics::highResamplingQuality);
                    }

                    result->aspect  = (float) bounds.getWidth() / (float) bounds.getHeight();
                    result->fingerX = fingerCount > 0 ? (fingerSum / (float) fingerCount - (float) bounds.getX()) / (float) bounds.getWidth() : 0.38f;
                    return result;
                }
            }
        }

        return nullptr;
    }();
    return icon.get();
}

//==============================================================================
void StringView::paint (juce::Graphics& g)
{
    // Background is drawn by the parent card.
    const auto y0 = stringY();
    const auto x0 = bridgeX();
    const auto x1 = nutX();

    // Faint damper grid: position in tenths across, pressure in quarters from
    // the zero-pressure dot height down to full pressure.
    {
        const auto dotCentreFor = [y0] (float pressure)
        {
            return y0 - juce::jmap (pressure, 0.0f, 1.0f, damperLiftLight, damperLiftHard) - markerSize * 0.5f;
        };
        const auto top = dotCentreFor (0.0f);

        g.setColour (colours::panelLine.withAlpha (0.45f));
        for (int i = 0; i <= 10; ++i)
        {
            const auto x = xForPosition ((float) i / 10.0f);
            g.drawLine (x, top, x, y0, i == 5 ? 1.0f : 0.6f);
        }
        for (int i = 0; i <= 4; ++i)
        {
            const auto y = dotCentreFor ((float) i / 4.0f);
            g.drawLine (x0, y, x1, y, 0.6f);
        }

        // String at rest
        g.setColour (colours::panelLine);
        g.drawLine (x0, y0, x1, y0, 1.0f);
    }

    // Midpoint tick
    const auto midX = xForPosition (0.5f);
    g.setColour (colours::panelLine);
    g.drawLine (midX, y0 - 10.0f, midX, y0 + 10.0f, 1.0f);

    drawAura (g);

    // Current shape plus faint earlier phases of the cycle, showing the swept envelope.
    auto shapeAt = [&] (float phaseOffset)
    {
        juce::Path shape;
        for (int i = 0; i <= numPoints; ++i)
        {
            const auto t = (float) i / (float) numPoints;
            const auto x = x0 + t * (x1 - x0);
            const auto y = y0 - displacementAt (t, phaseOffset);
            if (i == 0) shape.startNewSubPath (x, y); else shape.lineTo (x, y);
        }
        return shape;
    };

    if (level > 0.0f)
    {
        for (int k = 1; k <= numSnapshots; ++k)
        {
            const auto offset = (float) k / (float) (numSnapshots + 1);
            g.setColour (colours::panelInk.withAlpha (0.13f));
            g.strokePath (shapeAt (offset), juce::PathStrokeType (0.8f));
        }
    }

    // Hand is drawn behind the string.
    drawMarker (g, pick);

    g.setColour (colours::panelInk);
    g.strokePath (shapeAt (0.0f), juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bridge and nut
    g.fillEllipse (juce::Rectangle<float> (endRadius * 2.0f, endRadius * 2.0f).withCentre ({ x0, y0 }));
    g.fillEllipse (juce::Rectangle<float> (endRadius * 2.0f, endRadius * 2.0f).withCentre ({ x1, y0 }));

    drawMarker (g, damper);
}

int StringView::pluckFrame() const noexcept
{
    const auto frame = (int) (pluckAge / pluckFrameSeconds);
    return frame < pluckFrames ? frame : -1;
}

void StringView::drawMarker (juce::Graphics& g, int index) const
{
    const auto  box    = markerBounds (index);
    const bool  active = index == hoveredMarker || index == draggedMarker;
    const auto  y0     = stringY();

    const auto& positionBound = index == pick ? *pickPosition : *damperPosition;
    const auto  position = positionBound.value;
    // Damper is off at either end, or at zero pressure with no pressure LFO.
    const bool  off = index == damper && (mirroredPosition (position) <= 0.0005f
                                          || (damperPressure->value <= 0.0f && pressureLfoAmount->value <= 0.0f));
    const auto  x   = box.getCentreX();

    // At zero pressure the label stays visible to say the damper is off.
    const bool noPressure = index == damper && damperPressure->value <= 0.0f && pressureLfoAmount->value <= 0.0f
                            && mirroredPosition (position) > 0.0005f;
    auto fill = damperColour;
    if (off)
        fill = colours::panelDim;

    if (index == pick)
    {
        if (const auto* icon = handIcon())
        {
            const auto& image = icon->frames[(size_t) juce::jmax (0, pluckFrame())];

            // drawImage uses the current colour's alpha; reset it after the faint snapshots.
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (image, box, juce::RectanglePlacement::stretchToFit);
        }
    }
    else
    {
        // Damper: dot on a hairline stalk
        juce::Path dot;
        dot.addEllipse (box);
        g.setColour (colours::panelDim);
        g.drawLine (x, box.getBottom(), x, y0, 1.0f);
        g.setColour (fill);
        g.fillPath (dot);
        g.setColour (colours::panelInk);
        g.strokePath (dot, juce::PathStrokeType (1.0f));
    }

    // Label beside the marker while hovered or dragged, clamped to the bounds.
    if (active || noPressure)
    {
        auto text = juce::String (index == pick ? "Pick  " : "Damp  ")
                  + (index == damper && off ? juce::String ("off") : positionBound.parameter.getCurrentValueAsText());
        if (index == damper && ! off)
            text += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + damperPressure->parameter.getCurrentValueAsText();

        const auto font  = fonts::value();
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 4.0f;
        const auto labelY = index == pick ? box.getY() + box.getHeight() * 0.62f : box.getCentreY();

        auto labelBox = juce::Rectangle<float> (width, 14.0f).withCentre ({ 0.0f, labelY });
        labelBox.setX (box.getRight() + 8.0f);
        if (labelBox.getRight() > (float) getWidth() - 4.0f)      // flip left near the right edge
            labelBox.setX (box.getX() - 8.0f - width);
        labelBox.setY (juce::jlimit (2.0f, juce::jmax (2.0f, (float) getHeight() - 16.0f), labelBox.getY()));

        g.setFont (font);
        g.setColour (active ? colours::panelInk : colours::panelDim);   // dimmed when shown only for "off"
        g.drawText (text, labelBox, juce::Justification::centredLeft);
    }
}

//==============================================================================
float StringView::displayLevelFor (float audioLevel) noexcept
{
    // Square root keeps quiet notes visible; a single string peaks well below 1.0.
    return std::sqrt (juce::jlimit (0.0f, 1.0f, audioLevel * 2.5f));
}

void StringView::timerCallback()
{
    const auto incoming = displayLevelFor (levelSource());
    const auto previous = level;

    if (damperModulationSource)
    {
        const auto position = damperModulationSource();
        if (! juce::exactlyEqual (position, damperModulated))
        {
            damperModulated = position;
            repaint();
        }
    }

    if (pressureModulationSource)
    {
        const auto pressure = pressureModulationSource();
        if (! juce::exactlyEqual (pressure, pressureModulated))
        {
            pressureModulated = pressure;
            repaint();
        }
    }

    if (! auraBounds().isEmpty())
    {
        auraPhase = std::fmod (auraPhase + 0.05f, juce::MathConstants<float>::twoPi);   // about 2 s period
        repaint();
    }

    // Start the pluck animation on each new note-on.
    if (noteOnSource)
    {
        const auto count = noteOnSource();
        if (count != lastNoteOnCount)
        {
            lastNoteOnCount = count;
            pluckAge = 0.0f;
            repaint();
        }
    }

    if (pluckFrame() >= 0 && ! frozenForSnapshot)
    {
        pluckAge += secondsPerFrame;
        repaint();
    }

    // Level jump detected: restart the shape from the initial triangle.
    if (incoming > previous * 1.6f && incoming > 0.05f)
    {
        time = 0.0f;
        age  = 0.0f;
    }

    // Ease the pick deflection towards its target.
    const auto pressTarget = (draggedMarker == pick || hoveredMarker == pick) ? 1.0f : 0.0f;
    if (! juce::exactlyEqual (press, pressTarget))
    {
        press += (pressTarget - press) * 0.25f;
        if (std::abs (press - pressTarget) < 0.01f)
            press = pressTarget;
        repaint();
    }

    level = juce::jmax (incoming, level * levelDecay);
    if (level < 0.002f)
        level = 0.0f;

    if (level > 0.0f || previous > 0.0f)
    {
        time += periodsPerFrame;
        if (time >= 1.0f)
            time -= 1.0f;
        age = juce::jmin (age + secondsPerFrame, 10.0f);
        repaint();
    }
}

//==============================================================================
void StringView::mouseMove (const juce::MouseEvent& e)
{
    // Check the aura first: hovering it freezes the dot, which changes marker hit-testing.
    const auto inAura = auraBounds().expanded (10.0f).contains (e.position);
    if (inAura != auraHovered)
    {
        auraHovered = inAura;
        repaint();
    }

    const auto over = markerAt (e.getPosition());
    if (over != hoveredMarker)
    {
        hoveredMarker = over;
        setMouseCursor (over == damper ? juce::MouseCursor::UpDownLeftRightResizeCursor
                      : over >= 0      ? juce::MouseCursor::LeftRightResizeCursor
                                       : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StringView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredMarker >= 0 || auraHovered)
    {
        hoveredMarker = -1;
        auraHovered = false;
        repaint();
    }
}

void StringView::mouseDown (const juce::MouseEvent& e)
{
    draggedMarker = markerAt (e.getPosition());

    // Clicking near the bare string grabs the pick.
    if (draggedMarker < 0 && std::abs ((float) e.y - stringY()) < 14.0f)
        draggedMarker = pick;

    if (draggedMarker == pick)
    {
        pickPosition->attachment.beginGesture();
        pickPosition->attachment.setValueAsPartOfGesture (positionForX ((float) e.x));
    }
    else if (draggedMarker == damper)
    {
        damperPosition->attachment.beginGesture();
        damperPressure->attachment.beginGesture();
        dragStartPressure = damperPressure->value;
        dragStartY = e.y;
        damperPosition->attachment.setValueAsPartOfGesture (positionForX ((float) e.x));
    }
}

void StringView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedMarker == pick)
    {
        pickPosition->attachment.setValueAsPartOfGesture (positionForX ((float) e.x));
    }
    else if (draggedMarker == damper)
    {
        // Horizontal drag sets position, vertical drag sets pressure (down is harder).
        damperPosition->attachment.setValueAsPartOfGesture (positionForX ((float) e.x));
        const auto pressure = dragStartPressure + (float) (e.y - dragStartY) / pressurePixels;
        damperPressure->attachment.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, pressure));
    }
}

void StringView::mouseUp (const juce::MouseEvent&)
{
    if (draggedMarker == pick)
        pickPosition->attachment.endGesture();
    else if (draggedMarker == damper)
    {
        damperPosition->attachment.endGesture();
        damperPressure->attachment.endGesture();
    }

    draggedMarker = -1;
    repaint();
}

void StringView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto index = markerAt (e.getPosition());
    if (index < 0)
        return;

    auto reset = [] (Bound& b)
    {
        b.attachment.setValueAsCompleteGesture (b.parameter.convertFrom0to1 (b.parameter.getDefaultValue()));
    };

    if (index == pick)
        reset (*pickPosition);
    else
    {
        reset (*damperPosition);
        reset (*damperPressure);
    }
}

} // namespace pluck::ui
