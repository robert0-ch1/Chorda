/*
  ==============================================================================

    RenderTest.cpp

    Offline test harness for the Chorda processor: runs the plugin without a
    host, feeds it MIDI and checks the rendered audio.

    Usage:
      PluckRenderTest [output-dir]       run all tests, optionally writing each scenario as WAV
      PluckRenderTest --screenshot <file.png> [pluck frame 0..2]
                                         render the editor offscreen to PNG (README screenshot)
      PluckRenderTest --measure-tone     print peak and K-weighted level per exciter tone, note, attack
      PluckRenderTest --fuzz N           N renders with random parameters and notes
      PluckRenderTest --fuzz-live N      N runs with random parameter changes while playing

    The test run exits with the number of failed checks.

  ==============================================================================
*/

#include "Tests.h"

using namespace chorda_test;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 2 && juce::String (argv[1]) == "--fuzz-live")
        return fuzzLive (juce::String (argv[2]).getIntValue());

    if (argc > 2 && juce::String (argv[1]) == "--fuzz")
        return fuzz (juce::String (argv[2]).getIntValue());

    if (argc > 1 && juce::String (argv[1]) == "--measure-tone")
        return measureTone();

    if (argc > 2 && juce::String (argv[1]) == "--screenshot")
        return saveEditorScreenshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                                     argc > 3 ? juce::String (argv[3]).getIntValue() : -1);

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();

    std::cout << "Chorda render test" << std::endl;
    if (outDir != juce::File())
        std::cout << "Writing WAV files to " << outDir.getFullPathName() << std::endl;

    TestReport report;
    testReleaseHasNoClick (report, outDir);
    testReleaseTime (report, outDir);
    testHoldAndStability (report, outDir);
    testTuning (report);
    testStringControls (report, outDir);
    testPerformanceControls (report, outDir);
    testVoiceModes (report);
    testSustainAndDecay (report, outDir);
    testLowNotesSurviveShortPlucks (report, outDir);
    testPolyphonyIsIndependent (report, outDir);
    testVoiceStealing (report, outDir);
    testFactoryPresets (report, outDir);
    testStateRoundTrip (report);
    testSampleRateChange (report);
    testStereoWidth (report);
    testHeldNoteStaysClean (report, outDir);
    testSubTracksTheString (report, outDir);
    testPickIsAlwaysOnTheString (report);
    testToneCrossfade (report, outDir);
    testGlideAndLegato (report, outDir);
    testDamperKeepsTheEnvelope (report, outDir);
    testLowNotesLoseTheirTop (report, outDir);
    testHeldNotesDoNotClick (report, outDir);
    testNoteChangesDoNotClick (report, outDir);
    testCascadeEnergy (report);
    testDamperDragIsSmooth (report, outDir);
    testTheWholeString (report, outDir);
    testGlideLevel (report);
    testNoLatency (report);
    testBendIsClean (report);
    testPickWhileRinging (report);
    testSafety (report);
    testLfo (report, outDir);
    testReverb (report, outDir);
    testExciterLevels (report);

    std::cout << "\n" << report.passed << " passed, " << report.failed << " failed" << std::endl;
    return report.failed;
}
