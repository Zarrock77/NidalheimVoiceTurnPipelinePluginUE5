#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NidalheimVoiceTurnPipelineComponent.h"
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

#endif
