#include "NidalheimSpeechPlaybackBuffer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TArray<uint8> MakeSpeechPCM(int32 Frames, int32 FirstFrame = 0)
{
    TArray<uint8> PCM;
    PCM.SetNumUninitialized(Frames * sizeof(int16));
    for (int32 I = 0; I < Frames; ++I)
    {
        const int16 Value = 1 + (FirstFrame + I) % 32000;
        FMemory::Memcpy(PCM.GetData() + I * sizeof(int16), &Value, sizeof(Value));
    }
    return PCM;
}

void RenderSpeech(FNidalheimSpeechPlaybackBuffer& Buffer, double Now, TArray<uint8>& Heard, uint32 Frames = 120)
{
    TArray<uint8> Output;
    Output.SetNumUninitialized(Frames * sizeof(int16));
    const int64 Before = Buffer.GetStats().PlayedFrames;
    Buffer.Render(Output.GetData(), Frames, Now);
    const int64 Count = Buffer.GetStats().PlayedFrames - Before;
    Heard.Append(Output.GetData(), int32(Count * sizeof(int16)));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimSpeechBurstTest, "NidalheimVoiceTurnPipeline.Speech.BurstPreservesEverySample",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNidalheimSpeechBurstTest::RunTest(const FString& Parameters)
{
    // A 15-second answer generated at 5x playback speed, including uneven chunk sizes.
    FNidalheimSpeechPlaybackBuffer Buffer;
    Buffer.BeginSpeech();
    TArray<uint8> Heard;
    int32 Sent = 0;
    for (int32 Tick = 0; Tick < 3300; ++Tick)
    {
        const double Now = Tick * 0.005;
        if (Tick % 8 == 0 && Sent < 360000)
        {
            const int32 Frames = FMath::Min(Tick % 24 == 0 ? 9600 : 4800, 360000 - Sent);
            TestTrue(TEXT("Burst chunk accepted"), Buffer.Enqueue(MakeSpeechPCM(Frames, Sent), Now));
            Sent += Frames;
            if (Sent == 360000) Buffer.EndSpeech();
        }
        RenderSpeech(Buffer, Now, Heard);
    }
    TestTrue(TEXT("Every PCM sample played in order"), Heard == MakeSpeechPCM(360000));
    TestEqual(TEXT("No discarded speech"), Buffer.GetStats().CancelledFrames, int64(0));
    TestEqual(TEXT("No starvation after prefill"), Buffer.GetStats().Underruns, int64(0));
    TestEqual(TEXT("One completed reply"), Buffer.GetStats().CompletedReplies, int64(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimSpeechBoundaryTest, "NidalheimVoiceTurnPipeline.Speech.ShortAndConsecutiveReplies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNidalheimSpeechBoundaryTest::RunTest(const FString& Parameters)
{
    FNidalheimSpeechPlaybackBuffer Buffer;
    Buffer.BeginSpeech();
    Buffer.Enqueue(MakeSpeechPCM(37), 0);
    Buffer.EndSpeech();
    Buffer.BeginSpeech();
    Buffer.Enqueue(MakeSpeechPCM(401, 37), 0.01);
    Buffer.EndSpeech();
    TArray<uint8> Heard;
    for (int32 I = 0; I < 5; ++I) RenderSpeech(Buffer, I * 0.005, Heard);
    TestTrue(TEXT("Short tails and next reply survive without overwriting"), Heard == MakeSpeechPCM(438));
    TestEqual(TEXT("Both replies completed"), Buffer.GetStats().CompletedReplies, int64(2));
    TestEqual(TEXT("Natural end is not an underrun"), Buffer.GetStats().Underruns, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimSpeechJitterTest, "NidalheimVoiceTurnPipeline.Speech.PrefillRebufferAndFinalTail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNidalheimSpeechJitterTest::RunTest(const FString& Parameters)
{
    FNidalheimSpeechPlaybackBuffer Buffer;
    TArray<uint8> Heard;
    Buffer.BeginSpeech();
    Buffer.Enqueue(MakeSpeechPCM(1200), 0);
    RenderSpeech(Buffer, 0.01, Heard);
    TestTrue(TEXT("Wait for reserve"), Heard.IsEmpty());
    Buffer.Enqueue(MakeSpeechPCM(3600, 1200), 0.05);
    for (int32 I = 0; I < 42; ++I) RenderSpeech(Buffer, 0.05 + I * 0.005, Heard);
    TestEqual(TEXT("One starvation is counted, not one per empty callback"), Buffer.GetStats().Underruns, int64(1));
    Buffer.Enqueue(MakeSpeechPCM(1200, 4800), 0.5);
    RenderSpeech(Buffer, 0.51, Heard);
    TestEqual(TEXT("Rebuffer instead of playing tiny arrivals"), Heard.Num(), 4800 * 2);
    Buffer.EndSpeech();
    for (int32 I = 0; I < 12; ++I) RenderSpeech(Buffer, 0.6 + I * 0.005, Heard);
    TestTrue(TEXT("Final partial reserve is fully played"), Heard == MakeSpeechPCM(6000));
    TestEqual(TEXT("Queue drained"), Buffer.GetStats().QueuedFrames, int64(0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimSpeechResetTest, "NidalheimVoiceTurnPipeline.Speech.ResetLegacyAndLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNidalheimSpeechResetTest::RunTest(const FString& Parameters)
{
    FNidalheimSpeechPlaybackBuffer Buffer;
    Buffer.BeginSpeech();
    Buffer.Enqueue(MakeSpeechPCM(9600), 0);
    Buffer.Reset();
    Buffer.BeginSpeech();
    Buffer.Enqueue(MakeSpeechPCM(240, 15000), 0.1);
    Buffer.EndSpeech();
    TArray<uint8> Heard;
    for (int32 I = 0; I < 3; ++I) RenderSpeech(Buffer, 0.1 + I * 0.005, Heard);
    TestTrue(TEXT("Reconnect does not replay the previous NPC"), Heard == MakeSpeechPCM(240, 15000));
    TestEqual(TEXT("Explicit cancellation accounted"), Buffer.GetStats().CancelledFrames, int64(9600));

    TArray<uint8> Invalid; Invalid.Add(1);
    TestFalse(TEXT("Reject unaligned PCM"), Buffer.Enqueue(MoveTemp(Invalid), 1));
    TestFalse(TEXT("Reject excessive input atomically"), Buffer.Enqueue(MakeSpeechPCM(int32(Buffer.MaxQueuedFrames + 1)), 1));
    TestEqual(TEXT("Rejected input does not alter queued audio"), Buffer.GetStats().QueuedFrames, int64(0));

    Buffer.Reset();
    Buffer.Enqueue(MakeSpeechPCM(241), 2); // Old server: no audio_start or audio_end.
    Heard.Reset();
    RenderSpeech(Buffer, 2.1, Heard);
    TestTrue(TEXT("Legacy stream also prebuffers"), Heard.IsEmpty());
    for (int32 I = 0; I < 4; ++I) RenderSpeech(Buffer, 2.51 + I * 0.005, Heard);
    TestTrue(TEXT("Legacy short final chunk cannot remain stuck"), Heard == MakeSpeechPCM(241));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimSpeechConcurrencyTest, "NidalheimVoiceTurnPipeline.Speech.ConcurrentProducerAndConsumer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNidalheimSpeechConcurrencyTest::RunTest(const FString& Parameters)
{
    FNidalheimSpeechPlaybackBuffer Buffer;
    Buffer.BeginSpeech();
    std::atomic<bool> Done{false};
    auto Playback = Async(EAsyncExecution::Thread, [&Buffer, &Done]()
    {
        TArray<uint8> Heard;
        const double Deadline = FPlatformTime::Seconds() + 10;
        while ((!Done.load() || Buffer.GetStats().QueuedFrames > 0) && FPlatformTime::Seconds() < Deadline)
        {
            RenderSpeech(Buffer, FPlatformTime::Seconds(), Heard);
            FPlatformProcess::SleepNoStats(0);
        }
        return Heard;
    });
    for (int32 I = 0; I < 600; ++I)
        TestTrue(TEXT("Concurrent enqueue"), Buffer.Enqueue(MakeSpeechPCM(240, I * 240), FPlatformTime::Seconds()));
    Buffer.EndSpeech();
    Done.store(true);
    TestTrue(TEXT("Concurrent playback preserves all samples in FIFO order"), Playback.Get() == MakeSpeechPCM(144000));
    TestEqual(TEXT("All pending memory released"), Buffer.GetStats().QueuedFrames, int64(0));
    return true;
}

// Optional real-provider recording, supplied with -SpeechReplay=<jsonl path>.
// No external service is contacted by these tests and no audio is played on the device.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNidalheimSpeechRecordedTest, "NidalheimVoiceTurnPipeline.Speech.RecordedPCM",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FNidalheimSpeechRecordedTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    FString Path;
    if (FParse::Value(FCommandLine::Get(), TEXT("SpeechReplay="), Path))
    {
        Names.Add(TEXT("ProviderCapture"));
        Commands.Add(Path);
    }
}
bool FNidalheimSpeechRecordedTest::RunTest(const FString& Path)
{
    TArray<FString> Lines;
    if (!TestTrue(TEXT("Read supplied provider capture"), FFileHelper::LoadFileToStringArray(Lines, *Path))) return false;
    int32 Captures = 0;
    for (const FString& Line : Lines)
    {
        if (!Line.StartsWith(TEXT("{"))) continue;
        TSharedPtr<FJsonObject> Record;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Record)) return false;
        TArray<uint8> Source;
        if (!TestTrue(TEXT("Decode recorded PCM"), FBase64::Decode(Record->GetStringField(TEXT("pcm_base64")), Source))) return false;
        const auto& Chunks = Record->GetArrayField(TEXT("chunks"));
        const double DoneAt = Record->GetNumberField(TEXT("done_ms")) / 1000;
        FNidalheimSpeechPlaybackBuffer Buffer;
        Buffer.BeginSpeech();
        TArray<uint8> Heard;
        int32 Next = 0, Offset = 0;
        const double EndAt = DoneAt + Source.Num() / 48000.0 + 1;
        for (int32 Tick = 0; Tick * 0.005 < EndAt; ++Tick)
        {
            const double Now = Tick * 0.005;
            while (Next < Chunks.Num() && Chunks[Next]->AsObject()->GetNumberField(TEXT("at_ms")) <= Now * 1000)
            {
                const int32 Bytes = int32(Chunks[Next]->AsObject()->GetNumberField(TEXT("bytes")));
                if (Bytes < 0 || Offset + Bytes > Source.Num()) return false;
                TArray<uint8> PCM;
                PCM.Append(Source.GetData() + Offset, Bytes);
                if (!TestTrue(TEXT("Recorded chunk accepted"), Buffer.Enqueue(MoveTemp(PCM), Now))) return false;
                Offset += Bytes;
                Next++;
            }
            if (Now >= DoneAt) Buffer.EndSpeech();
            RenderSpeech(Buffer, Now, Heard);
        }
        TestTrue(TEXT("Recorded voice preserved byte for byte"), Source == Heard);
        TestEqual(TEXT("All capture chunks consumed"), Next, Chunks.Num());
        TestEqual(TEXT("No audio discarded"), Buffer.GetStats().CancelledFrames, int64(0));
        TestEqual(TEXT("No starvation with recorded arrivals"), Buffer.GetStats().Underruns, int64(0));
        AddInfo(FString::Printf(TEXT("%s: %d PCM frames preserved, %d chunks, zero loss"),
            *Record->GetStringField(TEXT("mode")), Source.Num() / 2, Chunks.Num()));
        Captures++;
    }
    TestTrue(TEXT("At least one real recording checked"), Captures > 0);
    return true;
}
#endif
