/*
  ==============================================================================

    PluginEditor.cpp

  ==============================================================================
*/

#include "PluginEditor.h"

using namespace pluck;
using namespace pluck::ui;

namespace
{
    // Layout grid.
    constexpr int margin        = 20;
    constexpr int headerHeight  = 52;
    constexpr int gap           = 12;
    constexpr int knobSize      = 54;    // knob disc diameter
    constexpr int knobHeight    = ParameterKnob::titleHeight + knobSize;
    constexpr int cardHeight    = SectionCard::captionHeight + SectionCard::padding * 2 + knobHeight;
    constexpr int voiceRowHeight = 26;   // voice row on the string panel
    constexpr int voiceGap       = 12;   // voice row to string
    constexpr int stringHeight  = 224;   // whole panel, voice row included
    constexpr int stringLift    = 6;     // clearance under the pick hand
    constexpr int titleWidth    = 84;    // "Chorda" in the title font
}

//==============================================================================
MainView::MainView (ChordaAudioProcessor& p)
    : apvts (p.getAPVTS()),
      presetBar (p.getPresetManager()),
      toneKnob       (apvts, ParamID::exciterTone,      "Exciter"),
      brightnessKnob (apvts, ParamID::stringBrightness, "Brightness"),
      subKnob        (apvts, ParamID::stringSub,        "Sub"),
      voiceModeBox   (apvts, ParamID::voiceMode,   "Voices", juce::Justification::centredLeft),
      glideBox       (apvts, ParamID::pitchGlide,  "Glide",  juce::Justification::centred),
      octaveBox      (apvts, ParamID::pitchOctave, "Octave", juce::Justification::centredRight),
      stringView     (apvts, [&p] { return p.getStringLevel(); },
                             [&p] { return p.getDamperModulation(); },
                             [&p] { return p.getPressureModulation(); },
                             [&p] { return p.getNoteOnCount(); }),
      attackKnob     (apvts, ParamID::exciterAttack,    "A"),
      decayKnob      (apvts, ParamID::stringDecay,      "D"),
      sustainKnob    (apvts, ParamID::stringSustain,    "S"),
      releaseKnob    (apvts, ParamID::stringRelease,    "R"),
      lfoAmountKnob  (apvts, ParamID::lfoAmount,        "Amount"),
      lfoRateKnob    (apvts, ParamID::lfoRate,          "Rate"),
      lfoPressureAmountKnob (apvts, ParamID::lfoPressureAmount, "Amount"),
      lfoPressureRateKnob   (apvts, ParamID::lfoPressureRate,   "Rate"),
      lfoTargetSwitch ({ "Position", "Pressure" },
                       [&a = apvts] (int i) { return a.getRawParameterValue (i == 0 ? ParamID::lfoAmount : ParamID::lfoPressureAmount)->load() > 0.0f; }),
      widthKnob      (apvts, ParamID::outputWidth,      "Width"),
      driveKnob      (apvts, ParamID::outputDrive,      "Drive"),
      reverbKnob     (apvts, ParamID::outputReverb,     "Reverb"),
      gainKnob       (apvts, ParamID::outputGain,       "Gain")
{
    addAndMakeVisible (presetBar);

    for (auto* card : { &stringCard, &timbreCard, &lfoCard, &envelopeCard, &outputCard })
        addAndMakeVisible (card);

    for (auto* knob : { &toneKnob, &brightnessKnob, &subKnob })
        timbreCard.addAndMakeVisible (knob);

    // Exciter value is drawn as its waveform.
    toneKnob.setValueDrawing (drawTone);

    for (auto* box : { &voiceModeBox, &glideBox, &octaveBox })
        stringCard.addAndMakeVisible (box);

    // Damper position and pressure LFOs, each with amount, rate and tempo sync.
    for (auto* knob : { &lfoAmountKnob, &lfoRateKnob, &lfoPressureAmountKnob, &lfoPressureRateKnob })
        lfoCard.addAndMakeVisible (knob);

    lfoCard.addAndMakeVisible (lfoSyncButton);
    lfoCard.addAndMakeVisible (lfoPressureSyncButton);
    lfoSyncAttachment         = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamID::lfoSync,         lfoSyncButton);
    lfoPressureSyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, ParamID::lfoPressureSync, lfoPressureSyncButton);

    // Synced rates display as note divisions; only the processor knows the tempo.
    lfoRateKnob.setValueTextSource         ([&p] { return p.getLfoRateText (false); });
    lfoPressureRateKnob.setValueTextSource ([&p] { return p.getLfoRateText (true); });

    lfoCard.addAndMakeVisible (lfoTargetSwitch);
    lfoTargetSwitch.onChange = [this] (int target) { showLfo (target); };
    showLfo (0);

    stringCard.addAndMakeVisible (stringView);

    for (auto* knob : { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob })
        envelopeCard.addAndMakeVisible (knob);
    for (auto* knob : { &widthKnob, &driveKnob, &reverbKnob, &gainKnob })
        outputCard.addAndMakeVisible (knob);

    setSize (width, height);
}

//==============================================================================
void MainView::paint (juce::Graphics& g)
{
    g.fillAll (colours::white);

    // Header hairline and title
    g.setColour (colours::hairline);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

    g.setColour (colours::ink);
    g.setFont (fonts::title());
    g.drawText ("Chorda", header.withTrimmedLeft (margin), juce::Justification::centredLeft);
}

//==============================================================================
void MainView::resized()
{
    auto bounds = getLocalBounds();

    // Header: title on the left, preset bar in the remaining width
    header = bounds.removeFromTop (headerHeight);
    presetBar.setBounds (header.reduced (margin, 11).withTrimmedLeft (titleWidth + 16));

    // Rows are sized to fill the content area exactly, so the margin is equal on all sides.
    auto content = bounds.reduced (margin);

    // --- String panel with voice row ---------------------------------------------
    {
        stringCard.setBounds (content.removeFromTop (stringHeight));
        auto inside = stringCard.getContentBounds();

        // Voice row spans bridge to nut so it aligns with the string.
        auto row = inside.removeFromTop (voiceRowHeight);
        const auto lineArea = row.withTrimmedLeft (StringView::postInset).withTrimmedRight (StringView::postInset);
        const auto cell = lineArea.getWidth() / 3;

        voiceModeBox.setBounds (lineArea.withWidth (cell));
        glideBox.setBounds     (lineArea.withWidth (cell).withCentre (lineArea.getCentre()));
        octaveBox.setBounds    (lineArea.withWidth (cell).withRightX (lineArea.getRight()));

        inside.removeFromTop (voiceGap);

        stringView.setBounds (inside.withTrimmedBottom (stringLift));
    }

    content.removeFromTop (gap);

    // Two equal columns.
    const auto leftColumn = (content.getWidth() - gap) / 2;

    // --- TIMBRE | ENVELOPE ------------------------------------------------------
    {
        auto row = content.removeFromTop (cardHeight);
        timbreCard.setBounds (row.removeFromLeft (leftColumn));
        row.removeFromLeft (gap);
        envelopeCard.setBounds (row);

        layoutKnobRow (timbreCard.getContentBounds(),   { &toneKnob, &brightnessKnob, &subKnob });
        layoutKnobRow (envelopeCard.getContentBounds(), { &attackKnob, &decayKnob, &sustainKnob, &releaseKnob });
    }

    content.removeFromTop (gap);

    // --- LFO | OUTPUT -----------------------------------------------------------
    {
        auto row = content.removeFromTop (cardHeight);
        lfoCard.setBounds (row.removeFromLeft (leftColumn));
        row.removeFromLeft (gap);
        outputCard.setBounds (row);

        // Switch | Amount | Rate in three even columns. Both LFOs share the same slots.
        {
            const auto area = lfoCard.getContentBounds();
            const auto column = area.getWidth() / 3;
            auto columnAt = [&] (int i) { return area.withX (area.getX() + i * column).withWidth (column); };

            layoutKnobRow (columnAt (1), { &lfoAmountKnob });
            layoutKnobRow (columnAt (1), { &lfoPressureAmountKnob });
            layoutKnobRow (columnAt (2), { &lfoRateKnob });
            layoutKnobRow (columnAt (2), { &lfoPressureRateKnob });

            // Sized like a knob: title above, knob-sized face below.
            lfoTargetSwitch.setBounds (columnAt (0).withSizeKeepingCentre (juce::jmin (column, knobSize + 24), knobHeight)
                                                   .withY (area.getY()));
        }
        layoutKnobRow (outputCard.getContentBounds(), { &widthKnob, &driveKnob, &reverbKnob, &gainKnob });

        // Sync buttons sit on the top-right corner of their rate knob.
        for (auto [button, knob] : { std::pair { &lfoSyncButton, &lfoRateKnob }, std::pair { &lfoPressureSyncButton, &lfoPressureRateKnob } })
            button->setBounds (knob->getBounds().withSize (16, 16)
                                   .withX (knob->getRight() - 14)
                                   .withY (knob->getY() + 2));
    }
}

void MainView::drawTone (juce::Graphics& g, juce::Rectangle<float> area, float tone)
{
    // Same crossfade as KarplusVoice::updateExciterMix: linear sine to square,
    // equal-power square to noise. Normalised to full height.
    tone = juce::jlimit (0.0f, 1.0f, tone);
    float sineGain = 0.0f, squareGain = 0.0f, noiseGain = 0.0f;
    if (tone <= toneSquare)
    {
        squareGain = tone / toneSquare;
        sineGain   = 1.0f - squareGain;
    }
    else
    {
        const auto angle = (tone - toneSquare) / (toneNoise - toneSquare) * juce::MathConstants<float>::halfPi;
        squareGain = std::cos (angle);
        noiseGain  = std::sin (angle);
    }

    // Two cycles; fixed seed keeps the noise static between repaints.
    constexpr int points = 64;
    juce::Random random (7);
    std::array<float, points + 1> samples {};
    float peak = 1.0e-3f;
    for (int i = 0; i <= points; ++i)
    {
        const auto phase = std::fmod (2.0f * (float) i / (float) points, 1.0f);
        const auto square = i == points ? 1.0f : (phase < 0.5f ? 1.0f : -1.0f);
        samples[(size_t) i] = sineGain   * std::sin (juce::MathConstants<float>::twoPi * phase)
                            + squareGain * square
                            + noiseGain  * (random.nextFloat() * 2.0f - 1.0f);
        peak = juce::jmax (peak, std::abs (samples[(size_t) i]));
    }

    const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 8.0f, 44.0f), area.getHeight() - 8.0f);
    juce::Path wave;
    for (int i = 0; i <= points; ++i)
    {
        const auto x = box.getX() + box.getWidth() * (float) i / (float) points;
        const auto y = box.getCentreY() - samples[(size_t) i] / peak * box.getHeight() * 0.5f;
        if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }

    g.setColour (colours::ink);
    g.strokePath (wave, juce::PathStrokeType (1.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

void MainView::showLfo (int target)
{
    const bool pressure = target == 1;
    lfoAmountKnob.setVisible (! pressure);
    lfoRateKnob.setVisible (! pressure);
    lfoSyncButton.setVisible (! pressure);
    lfoPressureAmountKnob.setVisible (pressure);
    lfoPressureRateKnob.setVisible (pressure);
    lfoPressureSyncButton.setVisible (pressure);
}

void MainView::layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    const auto count = (int) knobs.size();
    if (count == 0)
        return;

    const auto column = area.getWidth() / count;
    int i = 0;
    for (auto* knob : knobs)
    {
        const auto cell = area.withX (area.getX() + i * column).withWidth (column);
        knob->setBounds (cell.withSizeKeepingCentre (juce::jmin (column, knobSize + 24), knobHeight).withY (area.getY()));
        ++i;
    }
}

//==============================================================================
ChordaAudioProcessorEditor::ChordaAudioProcessorEditor (ChordaAudioProcessor& p)
    : AudioProcessorEditor (&p),
      mainView (p)
{
    // Add the view before setting the look-and-feel: JUCE only notifies existing children.
    addAndMakeVisible (mainView);
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (MainView::width * 3 / 5, MainView::height * 3 / 5,
                     MainView::width * 2,     MainView::height * 2);
    getConstrainer()->setFixedAspectRatio ((double) MainView::width / (double) MainView::height);

    setSize (MainView::width, MainView::height);
}

ChordaAudioProcessorEditor::~ChordaAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ChordaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::white);
}

void ChordaAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) MainView::width;
    mainView.setTransform (juce::AffineTransform::scale (scale));
    mainView.setBounds (0, 0, MainView::width, MainView::height);
}
