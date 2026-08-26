#include "TestFramework.h"

// ============================================================================
//  VoxBrain test runner.
//
//  Each suite lives in its own translation unit and registers its results with
//  vftest::Registry. Adding a suite is two lines: declare it here and add the
//  .cpp to the VoxBrainTests target in CMakeLists.txt.
//
//  Exit code 0 = everything passed, 1 = something failed, so CI and the
//  run_tests script can act on it without parsing output.
// ============================================================================

void runPitchTrackerTests();
void runNoteIntelligenceTests();
void runKeyDetectorTests();
void runRetuneEngineTests();
void runCompressorTests();
void runSmoothingTests();
void runVoiceChangerTests();
void runTuningAccuracyTests();
void runChainModuleTests();
void runRackModuleTests();
void runHostMatrixTests();
void runLayoutTests();

int main (int, char**)
{
    std::printf ("============================================================\n");
    std::printf ("  VoxBrain test suite\n");
    std::printf ("============================================================\n");

    // Ordered foundation-first: if the pitch tracker is broken, the failures in
    // everything downstream are consequences rather than separate faults.
    runPitchTrackerTests();
    runNoteIntelligenceTests();
    runKeyDetectorTests();
    runRetuneEngineTests();
    runCompressorTests();
    runSmoothingTests();
    runVoiceChangerTests();
    runTuningAccuracyTests();
    runChainModuleTests();
    runRackModuleTests();
    runHostMatrixTests();
    runLayoutTests();

    return vftest::Registry::instance().report();
}
