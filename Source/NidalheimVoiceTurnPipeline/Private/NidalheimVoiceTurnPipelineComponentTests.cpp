#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NidalheimVoiceTurnPipelineComponent.h"
#include "NidalheimSpeechPlaybackBuffer.h"
#include "Misc/Base64.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimVoiceTurnPipelineRoutingTest,
    "NidalheimVoiceTurnPipeline.Routing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNidalheimVoiceTurnPipelineRoutingTest::RunTest(const FString& Parameters)
{
    using FComponent = UNidalheimVoiceTurnPipelineComponent;

    // --- WebSocket URL: token first, agent second, `?` or `&` depending on the base ---
    TestEqual(TEXT("No token leaves the base URL untouched"),
        FComponent::BuildWsUrl(TEXT("wss://h/text"), FString(), TEXT("olaf"), TEXT("token"), TEXT("npc")),
        FString(TEXT("wss://h/text")));
    TestEqual(TEXT("Token then agent"),
        FComponent::BuildWsUrl(TEXT("wss://h/text"), TEXT("abc"), TEXT("olaf"), TEXT("token"), TEXT("npc")),
        FString(TEXT("wss://h/text?token=abc&npc=olaf")));
    TestEqual(TEXT("Base that already has a query string"),
        FComponent::BuildWsUrl(TEXT("wss://h/audio?x=1"), TEXT("abc"), TEXT("olaf"), TEXT("token"), TEXT("npc")),
        FString(TEXT("wss://h/audio?x=1&token=abc&npc=olaf")));
    TestEqual(TEXT("Token and agent are URL-encoded"),
        FComponent::BuildWsUrl(TEXT("wss://h/text"), TEXT("t&k"), TEXT("a b"), TEXT("token"), TEXT("npc")),
        FString(TEXT("wss://h/text?token=t%26k&npc=a%20b")));
    TestEqual(TEXT("Empty agent id adds no agent parameter"),
        FComponent::BuildWsUrl(TEXT("wss://h/text"), TEXT("abc"), FString(), TEXT("token"), TEXT("npc")),
        FString(TEXT("wss://h/text?token=abc")));
    TestEqual(TEXT("Parameter names are configurable"),
        FComponent::BuildWsUrl(TEXT("wss://h/text"), TEXT("abc"), TEXT("olaf"), TEXT("access_token"), TEXT("agent")),
        FString(TEXT("wss://h/text?access_token=abc&agent=olaf")));

    // --- Typed JSON is a server event, everything else is dialogue ---
    FComponent* Component = NewObject<FComponent>(GetTransientPackage());
    using EChannel = ENidalheimVoiceTurnChannel;
    TestFalse(TEXT("Plain text is dialogue"), Component->TryForwardTypedEvent(EChannel::Text, TEXT("Hello, traveller.")));
    TestFalse(TEXT("Malformed JSON is dialogue"), Component->TryForwardTypedEvent(EChannel::Text, TEXT("{not json")));
    TestFalse(TEXT("JSON without a type is dialogue"), Component->TryForwardTypedEvent(EChannel::Text, TEXT("{\"data\":1}")));
    TestTrue(TEXT("JSON with a type is a server event"),
        Component->TryForwardTypedEvent(EChannel::Text, TEXT("{\"type\":\"mission_started\",\"missionId\":\"m1\"}")));

    // --- The token provider is asked once per channel, and a failure does not wedge the connection ---
    int32 TokenRequests = 0;
    Component->TokenProvider.BindLambda([&TokenRequests](FNidalheimVoiceTurnTokenReady OnReady)
    {
        ++TokenRequests;
        OnReady.ExecuteIfBound(false, FString());
    });
    Component->Connect();
    TestEqual(TEXT("One token request per channel"), TokenRequests, 2);
    Component->Connect();
    TestEqual(TEXT("A failed attempt leaves the channels free to retry"), TokenRequests, 4);

    // Without a provider nothing can connect, but nothing crashes either: one logged error per
    // channel. (Unbinding also drops the lambda that references this stack frame.)
    AddExpectedError(TEXT("No TokenProvider bound"), EAutomationExpectedErrorFlags::Contains, 2);
    Component->TokenProvider.Unbind();
    Component->Connect();
    TestEqual(TEXT("No provider, no token request"), TokenRequests, 4);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimVoiceTurnPipelineAudioProtocolTest,
    "NidalheimVoiceTurnPipeline.AudioProtocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNidalheimVoiceTurnPipelineAudioProtocolTest::RunTest(const FString& Parameters)
{
    using FComponent = UNidalheimVoiceTurnPipelineComponent;

    // A hand-made queue stands in for the playback device, so no audio hardware is needed.
    FComponent* Component = NewObject<FComponent>(GetTransientPackage());
    FNidalheimSpeechPlaybackBuffer* Buffer = new FNidalheimSpeechPlaybackBuffer();
    Component->SpeechPlaybackBuffer = Buffer;
    ON_SCOPE_EXIT
    {
        Component->SpeechPlaybackBuffer = nullptr;
        delete Buffer;
    };

    TArray<uint8> Pcm;
    Pcm.SetNumZeroed(200); // 100 frames of silence
    const FString Chunk = FBase64::Encode(Pcm);
    auto AudioJson = [&Chunk](const TCHAR* RequestId)
    {
        return FString::Printf(TEXT("{\"type\":\"audio\",\"request_id\":\"%s\",\"data\":\"%s\"}"), RequestId, *Chunk);
    };
    auto StartJson = [](const TCHAR* RequestId, int32 SampleRate)
    {
        return FString::Printf(TEXT("{\"type\":\"audio_start\",\"request_id\":\"%s\",\"sample_rate\":%d,\"channels\":1,\"encoding\":\"pcm_s16le\"}"), RequestId, SampleRate);
    };

    // --- Normal reply: audio_start, audio, audio_end, then the device drains it ---
    Component->OnAudioMessageReceived(StartJson(TEXT("r1"), 24000));
    Component->OnAudioMessageReceived(AudioJson(TEXT("r1")));
    Component->OnAudioMessageReceived(TEXT("{\"type\":\"audio_end\",\"request_id\":\"r1\"}"));
    TestEqual(TEXT("The chunk was queued"), Buffer->GetStats().ReceivedFrames, int64(100));
    int16 Out[256];
    Buffer->Render(Out, 256, 0.0);
    TestEqual(TEXT("The reply played out"), Buffer->GetStats().PlayedFrames, int64(100));
    TestEqual(TEXT("The reply completed"), Buffer->GetStats().CompletedReplies, int64(1));

    // --- A chunk for another request id is ignored ---
    Component->OnAudioMessageReceived(StartJson(TEXT("r2"), 24000));
    Component->OnAudioMessageReceived(AudioJson(TEXT("bogus")));
    TestEqual(TEXT("Wrong request id queues nothing"), Buffer->GetStats().ReceivedFrames, int64(100));
    TestEqual(TEXT("Wrong request id leaves the queue empty"), Buffer->GetStats().QueuedFrames, int64(0));

    // --- A reply announcing another PCM format is rejected, and so are its chunks ---
    AddExpectedError(TEXT("Rejected TTS response"), EAutomationExpectedErrorFlags::Contains, 1);
    Component->OnAudioMessageReceived(StartJson(TEXT("r3"), 16000));
    TestTrue(TEXT("Invalid format is flagged"), Component->bRejectAudioReply);
    Component->OnAudioMessageReceived(AudioJson(TEXT("r3")));
    TestEqual(TEXT("Rejected reply queues nothing"), Buffer->GetStats().ReceivedFrames, int64(100));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNidalheimVoiceTurnPipelineReconnectTest,
    "NidalheimVoiceTurnPipeline.Reconnect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNidalheimVoiceTurnPipelineReconnectTest::RunTest(const FString& Parameters)
{
    using FComponent = UNidalheimVoiceTurnPipelineComponent;

    // --- Backoff: base, x2 per failure, capped at 30 s even for absurd counts ---
    TestEqual(TEXT("First retry waits the base delay"), FComponent::ComputeReconnectDelay(0.5f, 0), 0.5f);
    TestEqual(TEXT("Second retry doubles"), FComponent::ComputeReconnectDelay(0.5f, 1), 1.f);
    TestEqual(TEXT("Fourth retry"), FComponent::ComputeReconnectDelay(0.5f, 3), 4.f);
    TestEqual(TEXT("Capped at 30 s"), FComponent::ComputeReconnectDelay(0.5f, 6), 30.f);
    TestEqual(TEXT("No overflow on a huge count"), FComponent::ComputeReconnectDelay(0.5f, 100000), 30.f);

    // --- Pending sends: bounded at 64, the oldest is dropped ---
    TArray<FString> Queue;
    for (int32 i = 0; i < 70; ++i)
    {
        FComponent::AppendPendingSend(Queue, FString::FromInt(i));
    }
    TestEqual(TEXT("Queue is capped"), Queue.Num(), 64);
    TestEqual(TEXT("Oldest messages were dropped"), Queue[0], FString(TEXT("6")));
    TestEqual(TEXT("Newest message is kept"), Queue.Last(), FString(TEXT("69")));

    // --- Switching agent drops what was queued for the previous one ---
    FComponent* Component = NewObject<FComponent>(GetTransientPackage());
    Component->TokenProvider.BindLambda([](FNidalheimVoiceTurnTokenReady OnReady)
    {
        OnReady.ExecuteIfBound(false, FString());
    });
    Component->SendRawJson(TEXT("{\"type\":\"sync\"}"));
    TestEqual(TEXT("Message queued while the channel is down"), Component->PendingTextSends.Num(), 1);
    Component->SetAgentId(TEXT("olaf"));
    TestEqual(TEXT("SetAgentId empties the queue"), Component->PendingTextSends.Num(), 0);
    Component->TokenProvider.Unbind();

    return true;
}

#endif
