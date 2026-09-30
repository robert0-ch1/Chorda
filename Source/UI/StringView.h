/*
  ==============================================================================

    StringView.h

    A drawing of the string itself: bridge on the left, nut on the right, and
    the string between them.

    While notes sound, the string moves the way a real plucked string does.
    At the moment of the pluck it is a triangle with its apex at the pick;
    its partials then swing at their own rates and the high ones die first,
    faster with a darker Brightness, so the shape relaxes into the smooth bow
    of the fundamental. A finger on the string pins it to a node under the
    damper.

    Two markers on the string are live controls, each bound to a parameter:

        pick   (pointer hand below the string)   where the string is plucked, and
                                                 where it is heard from; the string
                                                 gives under the finger while you
                                                 hold it, and keeps its kink there.
                                                 The fingertip rests just touching
                                                 the string, always behind it; at
                                                 every note-on the hand plays a
                                                 three-frame pluck, rising a little
                                                 as the finger flicks
        damper (dot above the string)            where a finger touches, and how
                                                 hard: drag sideways to move it,
                                                 up and down to change pressure

    Both run over the whole string, bridge (0 %) to nut (100 %). Past the
    middle a position mirrors the one before it, as on a real string: the same
    harmonics, the pluck's phases the other way round. Double-click a marker to
    reset it. The damper at either end is off; the pick is always on the string.

    The LFOs swing the damper either side of where it is set, and the dot
    moves with them, so you see the finger where it really is. A soft blue
    glow marks the whole area they reach. Hover over that area and the dot
    stops at its set place, so it can be seen and dragged; move away and it
    moves again.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>

namespace pluck::ui
{

class StringView final : public juce::Component,
                         private juce::Timer
{
public:
    /** @param levelSource   returns the current string level (0..1-ish), polled on a timer */
    /** @param damperModulationSource  returns where the position LFO has the damper, or a negative value while it is idle; polled on a timer */
    StringView (juce::AudioProcessorValueTreeState& apvts, std::function<float()> levelSource,
                std::function<float()> damperModulationSource = {},
                std::function<float()> pressureModulationSource = {},
                std::function<int()> noteOnSource = {});
    ~StringView() override;

    void paint (juce::Graphics&) override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Holds the pluck on one frame (0, 1, 2), for still renders such as the
        README screenshot, which cannot run the animation's timer. */
    void showPluckFrameForSnapshot (int frame)   { pluckAge = ((float) frame + 0.5f) * 0.1f; frozenForSnapshot = true; repaint(); }

    /** Distance of the string's ends from our edges. The voice line above
        uses it to sit square over the string. */
    static constexpr int postInset = 44;

private:
    enum MarkerIndex { pick = 0, damper, numMarkers };

    /** One parameter bound to a marker. */
    struct Bound
    {
        Bound (juce::RangedAudioParameter& p, std::function<void()> onChange)
            : parameter (p),
              attachment (p, [this, onChange] (float v) { value = v; onChange(); })
        {
            attachment.sendInitialUpdate();
        }

        juce::RangedAudioParameter& parameter;
        juce::ParameterAttachment attachment;
        float value = 0.0f;
    };

    void timerCallback() override;

    float bridgeX() const noexcept;
    float nutX() const noexcept;
    float stringY() const noexcept;
    float xForPosition (float position) const noexcept;
    float positionForX (float x) const noexcept;
    juce::Rectangle<float> markerBounds (int index) const noexcept;
    int   markerAt (juce::Point<int> p) const noexcept;
    void  drawMarker (juce::Graphics&, int index) const;
    static void drawPointerHand (juce::Graphics&, juce::Rectangle<float> box, juce::Colour line);
    struct HandIcon;
    static const HandIcon* handIcon();

    /** String displacement at x (0..1) for the current animation time. */
    float displacementAt (float x, float phaseOffset = 0.0f) const noexcept;

    /** Where the finger really is: the damper position moved by the LFO. */
    float movingDamperPosition() const noexcept;

    /** True while the dot is held still at its set place for editing. */
    bool  damperFrozen() const noexcept;

    /** Where the dot is drawn: moving with the LFOs, or at its set place while frozen. */
    float shownDamperPosition() const noexcept;
    float shownDamperPressure() const noexcept;

    /** The area the LFOs can take the dot through, empty while they are idle. */
    juce::Rectangle<float> auraBounds() const noexcept;
    void  drawAura (juce::Graphics&) const;

    /** Maps an audio level to a 0..1 drawing amplitude. */
    static float displayLevelFor (float audioLevel) noexcept;

    juce::RangedAudioParameter& brightnessParameter;
    std::unique_ptr<Bound> pickPosition, damperPosition, damperPressure, positionLfoAmount, pressureLfoAmount;
    std::function<float()> levelSource, damperModulationSource, pressureModulationSource;
    std::function<int()>   noteOnSource;
    int   lastNoteOnCount = 0;
    float pluckAge          = 1.0f;    ///< seconds since the hand last plucked; the animation runs for three frames
    bool  frozenForSnapshot = false;

    /** Which frame of the pluck is showing (0, 1, 2), or -1 at rest. */
    int   pluckFrame() const noexcept;
    float damperModulated = -1.0f;   ///< where the position LFO has the damper, negative while idle
    float pressureModulated = -1.0f; ///< how hard the pressure LFO has it pressing, negative while idle
    bool  auraHovered = false;       ///< the mouse is over the LFOs' area, so the dot holds still
    float auraPhase   = 0.0f;        ///< the glow breathes slowly
    float press       = 0.0f;        ///< 0..1, how far the string gives under a held finger

    float level         = 0.0f;   ///< smoothed string level for the animation
    float time          = 0.0f;   ///< animation phase in fundamental periods
    float age           = 0.0f;   ///< seconds since the last pluck, for the partials' decay
    int   hoveredMarker = -1;
    int   draggedMarker = -1;
    float dragStartPressure = 0.0f;
    int   dragStartY        = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringView)
};

} // namespace pluck::ui
