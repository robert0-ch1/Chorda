/*
  ==============================================================================

    RenderTest.cpp

    Offline test harness for the Chorda processor. It instantiates the
    plugin without a host, feeds it MIDI, and inspects the rendered audio:

      1. note-off produces a smooth release, not a click, and sustain /
         decay behave as string damping;
      2. releasing one note does not silence the others;
      3. hammering more notes than there are voices neither crashes nor
         produces NaNs;
      4. every factory preset renders finite audio;
      5. host state survives a save / restore round trip;
      6. a sample-rate change is handled.

    Usage:  PluckRenderTest [output-directory]
    If a directory is given, the rendered scenarios are written there as WAV
    files so you can listen to them.

            PluckRenderTest --screenshot <file.png> [pluck frame 0..2]
    Renders the editor offscreen at its default size and saves it as a PNG,
    optionally with the pick hand held on one frame of its pluck.
    Used to keep the README screenshot in sync with the real UI.

    Exit code is the number of failed checks (0 = all good).

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
