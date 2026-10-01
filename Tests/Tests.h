/*
  ==============================================================================

    Tests.h

    The test functions and tools, called from main() in RenderTest.cpp.

  ==============================================================================
*/

#pragma once

#include "TestHelpers.h"

namespace chorda_test
{

void testReleaseHasNoClick (TestReport& report, const juce::File& outDir);
void testHoldAndStability (TestReport& report, const juce::File& outDir);
void testReleaseTime (TestReport& report, const juce::File& outDir);
void testSustainAndDecay (TestReport& report, const juce::File& outDir);
void testLowNotesSurviveShortPlucks (TestReport& report, const juce::File& outDir);
void testHeldNoteStaysClean (TestReport& report, const juce::File& outDir);
void testDamperKeepsTheEnvelope (TestReport& report, const juce::File& outDir);
void testHeldNotesDoNotClick (TestReport& report, const juce::File& outDir);
void testTuning (TestReport& report);
void testStringControls (TestReport& report, const juce::File& outDir);
void testSubTracksTheString (TestReport& report, const juce::File& outDir);
void testPickIsAlwaysOnTheString (TestReport& report);
void testToneCrossfade (TestReport& report, const juce::File& outDir);
void testLowNotesLoseTheirTop (TestReport& report, const juce::File& outDir);
void testDamperDragIsSmooth (TestReport& report, const juce::File& outDir);
void testTheWholeString (TestReport& report, const juce::File& outDir);
void testBendIsClean (TestReport& report);
void testPickWhileRinging (TestReport& report);
void testNoLatency (TestReport& report);
void testExciterLevels (TestReport& report);
void testCascadeEnergy (TestReport& report);
void testPerformanceControls (TestReport& report, const juce::File& outDir);
void testVoiceModes (TestReport& report);
void testPolyphonyIsIndependent (TestReport& report, const juce::File& outDir);
void testVoiceStealing (TestReport& report, const juce::File& outDir);
void testGlideAndLegato (TestReport& report, const juce::File& outDir);
void testGlideLevel (TestReport& report);
juce::AudioBuffer<float> renderWatchingLfo (ChordaAudioProcessor& processor, std::vector<MidiEvent> events, int totalSamples, std::vector<float>& lfo);
void testLfo (TestReport& report, const juce::File& outDir);
void testNoteChangesDoNotClick (TestReport& report, const juce::File& outDir);
void testFactoryPresets (TestReport& report, const juce::File& outDir);
void testStateRoundTrip (TestReport& report);
void testSampleRateChange (TestReport& report);
void testStereoWidth (TestReport& report);
void testSafety (TestReport& report);
void testReverb (TestReport& report, const juce::File& outDir);
pluck::ui::StringView* findStringView (juce::Component& parent);
int saveEditorScreenshot (const juce::File& file, int pluckFrame = -1);
int measureTone();
int fuzz (int count);
int fuzzLive (int count);

} // namespace chorda_test
