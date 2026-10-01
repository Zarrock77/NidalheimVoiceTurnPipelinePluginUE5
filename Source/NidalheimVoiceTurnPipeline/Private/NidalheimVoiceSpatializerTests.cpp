#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NidalheimVoiceSpatializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimVoiceSpatializerTest,
    "NidalheimVoiceTurnPipeline.Spatializer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNidalheimVoiceSpatializerTest::RunTest(const FString& Parameters)
{
    using FSpat = FNidalheimVoiceSpatializer;
    const FVector Origin = FVector::ZeroVector;
    const FVector Right = FVector::RightVector; // the listener faces +X, its right is +Y
    float Gain = 0.f, Pan = 0.f;

    // --- Distance attenuation ---
    FSpat::Compute(FVector(100, 0, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("Inside the min distance: full volume"), Gain, 1.f);
    FSpat::Compute(FVector(1000, 0, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("At the max distance: silent"), Gain, 0.f);
    FSpat::Compute(FVector(5000, 0, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("Past the max distance: silent"), Gain, 0.f);
    FSpat::Compute(FVector(600, 0, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("Halfway, linear rolloff: half volume"), Gain, 0.5f);
    float Steeper = 0.f;
    FSpat::Compute(FVector(600, 0, 0), Origin, Right, 200.f, 1000.f, 2.f, Steeper, Pan);
    TestTrue(TEXT("A higher rolloff is quieter at the same distance"), Steeper < Gain);

    // --- Balance ---
    FSpat::Compute(FVector(500, 0, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("Straight ahead: centered"), Pan, 0.f);
    FSpat::Compute(FVector(0, 500, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("On the right: pans right, capped"), Pan, FSpat::MaxPan);
    FSpat::Compute(FVector(0, -500, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestEqual(TEXT("On the left: pans left, capped"), Pan, -FSpat::MaxPan);
    FSpat::Compute(FVector(0, 100, 0), Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestTrue(TEXT("Inside the near zone the pan eases in"), Pan > 0.f && Pan < FSpat::MaxPan);
    FSpat::Compute(Origin, Origin, Right, 200.f, 1000.f, 1.f, Gain, Pan);
    TestTrue(TEXT("On top of the listener: centered, no NaN"), Pan == 0.f && Gain == 1.f);

    // --- Mixing ---
    const int16 Mono[4] = {1000, 1000, 1000, 1000};
    int16 Stereo[8] = {};

    FSpat Centered;
    Centered.SetTarget(1.f, 0.f);
    Centered.MixToStereo(Mono, Stereo, 4);
    TestTrue(TEXT("Centered full volume is the old mono level on both channels"),
        Stereo[0] == 1000 && Stereo[1] == 1000 && Stereo[6] == 1000 && Stereo[7] == 1000);

    FSpat PannedRight;
    PannedRight.SetTarget(1.f, 0.5f);
    PannedRight.MixToStereo(Mono, Stereo, 4); // the first block snaps to the target
    TestEqual(TEXT("Panned right: near ear untouched"), static_cast<int32>(Stereo[1]), 1000);
    TestEqual(TEXT("Panned right: far ear halved"), static_cast<int32>(Stereo[0]), 500);

    FSpat Quiet;
    Quiet.SetTarget(0.f, 0.f);
    Quiet.MixToStereo(Mono, Stereo, 4);
    TestTrue(TEXT("Zero gain is silence"), Stereo[0] == 0 && Stereo[1] == 0 && Stereo[7] == 0);

    // A target change ramps across the block instead of jumping.
    Quiet.SetTarget(1.f, 0.f);
    Quiet.MixToStereo(Mono, Stereo, 4);
    TestTrue(TEXT("Ramp: starts quiet, ends at the target"), Stereo[0] < 500 && Stereo[6] == 1000);

    return true;
}

#endif
