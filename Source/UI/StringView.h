/*
  ==============================================================================

    StringView.h

    Animated string between bridge (left) and nut (right), with two draggable
    markers: the pick (hand below the string) and the damper (dot above it).
    The displacement is a sum of decaying partials seeded by a triangular
    pluck shape; the damper's LFO range is drawn as a glow.

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
    /** All sources are polled on the UI timer.
        @param levelSource             current string level, roughly 0..1
        @param damperModulationSource  LFO-modulated damper position, negative while idle */
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

    /** Freezes the pluck animation on frame 0, 1 or 2 for offline snapshots. */
    void showPluckFrameForSnapshot (int frame)   { pluckAge = ((float) frame + 0.5f) * 0.1f; frozenForSnapshot = true; repaint(); }

    /** Inset of the string ends from the component edges; shared with the voice row layout. */
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
    struct HandIcon;
    static const HandIcon* handIcon();

    /** String displacement at x (0..1) for the current animation time. */
    float displacementAt (float x, float phaseOffset = 0.0f) const noexcept;

    /** Damper position including LFO modulation. */
    float movingDamperPosition() const noexcept;

    /** True while the dot is held at its set position for editing (aura hovered). */
    bool  damperFrozen() const noexcept;

    /** Drawn damper position and pressure: modulated, or the set values while frozen. */
    float shownDamperPosition() const noexcept;
    float shownDamperPressure() const noexcept;

    /** Area the LFOs can move the dot through; empty while they are idle. */
    juce::Rectangle<float> auraBounds() const noexcept;
    void  drawAura (juce::Graphics&) const;

    /** Maps an audio level to a 0..1 drawing amplitude. */
    static float displayLevelFor (float audioLevel) noexcept;

    juce::RangedAudioParameter& brightnessParameter;
    std::unique_ptr<Bound> pickPosition, damperPosition, damperPressure, positionLfoAmount, pressureLfoAmount;
    std::function<float()> levelSource, damperModulationSource, pressureModulationSource;
    std::function<int()>   noteOnSource;
    int   lastNoteOnCount = 0;
    float pluckAge          = 1.0f;    ///< seconds since the last note-on, drives the pluck frames
    bool  frozenForSnapshot = false;

    /** Current pluck frame (0, 1, 2), or -1 at rest. */
    int   pluckFrame() const noexcept;
    float damperModulated = -1.0f;   ///< modulated position, negative while idle
    float pressureModulated = -1.0f; ///< modulated pressure, negative while idle
    bool  auraHovered = false;       ///< mouse over the LFO area
    float auraPhase   = 0.0f;        ///< glow pulse phase
    float press       = 0.0f;        ///< 0..1 string deflection under a held pick

    float level         = 0.0f;   ///< smoothed string level for the animation
    float time          = 0.0f;   ///< animation phase in fundamental periods
    float age           = 0.0f;   ///< seconds since the last pluck, for partial decay
    int   hoveredMarker = -1;
    int   draggedMarker = -1;
    float dragStartPressure = 0.0f;
    int   dragStartY        = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringView)
};

} // namespace pluck::ui
