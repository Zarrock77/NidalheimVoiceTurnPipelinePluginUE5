#include "NidalheimVoiceTurnPipelineComponent.h"
#include "NidalheimSpeechPlaybackBuffer.h"
#include "NidalheimVoiceSpatializer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SocketSubsystem.h"
#include "AddressInfoTypes.h"
#include "UObject/UObjectIterator.h"

#include "miniaudio.h"

DEFINE_LOG_CATEGORY_STATIC(LogNidalheimVoiceTurn, Log, All);

namespace
{
    // Capture is mono PCM16; the rate is UNidalheimVoiceTurnPipelineComponent::CaptureSampleRate and is
    // declared to the backend in `audio_config`. miniaudio converts from the device's native rate
    // (e.g. 16 kHz on Bluetooth HFP, 44.1 kHz on cheap mics), so we always emit at that rate.
    constexpr ma_uint32 kCaptureChannels = 1;

    // Playback format. The backend's TTS emits 24 kHz mono PCM16 by default and announces it in
    // `audio_start`; receiving at the source rate avoids server-side resampling. miniaudio handles
    // any conversion to the playback device's native rate.
    constexpr ma_uint32 kPlaybackSampleRate = 24000;
    constexpr ma_uint32 kPlaybackChannels = 1;
    // The device itself is stereo so the voice can be balanced; the TTS stream stays mono.
    constexpr ma_uint32 kOutputChannels = 2;
    // How often the speaker/listener positions are republished to the audio thread.
    constexpr float kSpatialUpdateSeconds = 1.f / 30.f;
    static_assert(kPlaybackSampleRate == FNidalheimSpeechPlaybackBuffer::SampleRate);

    // Voice volume (0..1), shared by every pipeline: the setting belongs to the player, not to a
    // character. Written from the game thread (menu), read from the game thread (device start);
    // ma_device_set_master_volume is itself safe against the audio thread.
    float GVoicePlaybackVolume = 1.f;
}

UNidalheimVoiceTurnPipelineComponent::UNidalheimVoiceTurnPipelineComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNidalheimVoiceTurnPipelineComponent::BeginPlay()
{
    Super::BeginPlay();
    bShuttingDown = false;

    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Voice-turn pipeline started (agent=%s, autoConnect=%d)"), *AgentId, bAutoConnect ? 1 : 0);

    // Audio I/O is miniaudio (WASAPI direct) for both capture and playback: no engine AudioMixer,
    // SynthComponent or SoundWaveProcedural. The capture device is created lazily on push-to-talk;
    // the playback device is created in OnAudioConnected so it is ready for the first
    // {type:"audio"} chunk.
    if (bAutoConnect)
    {
        Connect();
    }
}

void UNidalheimVoiceTurnPipelineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bShuttingDown = true;

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TextReconnectTimer);
    }

    StopAudioCapture();
    TearDownMiniAudioPlayback();

    if (TextWebSocket.IsValid())
    {
        TextWebSocket->Close();
        TextWebSocket.Reset();
    }

    CloseAudioWebSocket();

    Super::EndPlay(EndPlayReason);
}

void UNidalheimVoiceTurnPipelineComponent::Connect()
{
    ConnectTextWebSocket();
    ConnectAudioWebSocket();
}

FString UNidalheimVoiceTurnPipelineComponent::BuildWsUrl(const FString& BaseUrl, const FString& AccessToken,
    const FString& InAgentId, const FString& TokenParam, const FString& AgentParam)
{
    if (BaseUrl.IsEmpty() || AccessToken.IsEmpty()) return BaseUrl;
    FString Url = BaseUrl;
    {
        const TCHAR Joiner = Url.Contains(TEXT("?")) ? TEXT('&') : TEXT('?');
        Url = FString::Printf(TEXT("%s%c%s=%s"), *Url, Joiner, *TokenParam, *FGenericPlatformHttp::UrlEncode(AccessToken));
    }
    if (!InAgentId.IsEmpty())
    {
        Url = FString::Printf(TEXT("%s&%s=%s"), *Url, *AgentParam, *FGenericPlatformHttp::UrlEncode(InAgentId));
    }
    return Url;
}

FString UNidalheimVoiceTurnPipelineComponent::ResolveBaseUrl(const FString& OverrideUrl, const TCHAR* Key, const TCHAR* LegacyKey) const
{
    if (!OverrideUrl.IsEmpty())
    {
        return OverrideUrl;
    }
    const FString Section = ConfigSection.IsEmpty() ? FString(FApp::GetProjectName()) : ConfigSection;
    const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini");
    FString Value;
    GConfig->GetString(*Section, Key, Value, ConfigPath);
    if (Value.IsEmpty())
    {
        GConfig->GetString(*Section, LegacyKey, Value, ConfigPath);
    }
    return Value;
}

void UNidalheimVoiceTurnPipelineComponent::RequestToken(FNidalheimVoiceTurnTokenReady OnReady)
{
    if (!TokenProvider.IsBound())
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("No TokenProvider bound - cannot get an access token"));
        OnReady.ExecuteIfBound(false, FString());
        return;
    }
    TokenProvider.Execute(MoveTemp(OnReady));
}

void UNidalheimVoiceTurnPipelineComponent::LogResolvedAddress(const FString& WebSocketURL) const
{
    FString URLWithoutScheme = WebSocketURL;
    URLWithoutScheme.RemoveFromStart(TEXT("wss://"));
    URLWithoutScheme.RemoveFromStart(TEXT("ws://"));

    FString HostPort;
    URLWithoutScheme.Split(TEXT("/"), &HostPort, nullptr);
    if (HostPort.IsEmpty())
    {
        HostPort = URLWithoutScheme;
    }

    FString Host;
    HostPort.Split(TEXT(":"), &Host, nullptr);
    if (Host.IsEmpty())
    {
        Host = HostPort;
    }

    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSub)
    {
        return;
    }

    const FAddressInfoResult Info = SocketSub->GetAddressInfo(
        *Host,
        nullptr,
        EAddressInfoFlags::Default,
        NAME_None);

    if (Info.ReturnCode == SE_NO_ERROR && Info.Results.Num() > 0)
    {
        const TSharedRef<FInternetAddr>& Addr = Info.Results[0].Address;
        UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("DNS resolved: %s -> %s"), *Host, *Addr->ToString(false));
    }
    else
    {
        UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("DNS resolution failed for %s (error: %d)"), *Host, static_cast<int32>(Info.ReturnCode));
    }
}

void UNidalheimVoiceTurnPipelineComponent::ConnectTextWebSocket()
{
    if (bShuttingDown || bTextConnecting)
    {
        return;
    }
    if (TextWebSocket.IsValid()) // already open (connecting or connected)
    {
        return;
    }

    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    bTextConnecting = true;
    TWeakObjectPtr<UNidalheimVoiceTurnPipelineComponent> WeakSelf(this);
    RequestToken(FNidalheimVoiceTurnTokenReady::CreateLambda(
        [WeakSelf](bool bSuccess, const FString& Token)
        {
            UNidalheimVoiceTurnPipelineComponent* Self = WeakSelf.Get();
            if (!Self) return;
            Self->bTextConnecting = false;
            if (Self->bShuttingDown) return;
            if (!bSuccess || Token.IsEmpty())
            {
                UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Text WebSocket connect aborted - no token, retrying"));
                Self->ScheduleTextReconnect();
                return;
            }
            Self->OpenTextSocketWithToken(Token);
        }));
}

void UNidalheimVoiceTurnPipelineComponent::ConnectAudioWebSocket()
{
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    TWeakObjectPtr<UNidalheimVoiceTurnPipelineComponent> WeakSelf(this);
    RequestToken(FNidalheimVoiceTurnTokenReady::CreateLambda(
        [WeakSelf](bool bSuccess, const FString& Token)
        {
            UNidalheimVoiceTurnPipelineComponent* Self = WeakSelf.Get();
            if (!Self || Self->bShuttingDown) return;
            if (!bSuccess || Token.IsEmpty())
            {
                UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Audio WebSocket connect aborted - no access token"));
                return;
            }
            Self->OpenAudioSocketWithToken(Token);
        }));
}

void UNidalheimVoiceTurnPipelineComponent::OpenTextSocketWithToken(const FString& AccessToken)
{
    const FString BaseUrl = ResolveBaseUrl(TextBaseUrl, TEXT("WebsocketTextBaseUrl"), TEXT("WebsocketTextEndpoint"));
    if (BaseUrl.IsEmpty())
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("WebsocketTextBaseUrl not configured (set TextBaseUrl or [%s] WebsocketTextBaseUrl in DefaultGame.ini)"),
            ConfigSection.IsEmpty() ? FApp::GetProjectName() : *ConfigSection);
        return;
    }

    const FString Url = BuildWsUrl(BaseUrl, AccessToken, AgentId, TokenQueryParam, AgentQueryParam);

    TextWebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, FString());
    TextWebSocket->OnConnected().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnTextConnected);
    TextWebSocket->OnConnectionError().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnTextConnectionError);
    TextWebSocket->OnClosed().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnTextClosed);
    TextWebSocket->OnMessage().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnTextMessageReceived);

    LogResolvedAddress(BaseUrl);
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Connecting text WebSocket (auth'd) to base: %s"), *BaseUrl);
    TextWebSocket->Connect();
}

void UNidalheimVoiceTurnPipelineComponent::OpenAudioSocketWithToken(const FString& AccessToken)
{
    if (bShuttingDown) return;
    const FString BaseUrl = ResolveBaseUrl(AudioBaseUrl, TEXT("WebsocketAudioBaseUrl"), TEXT("WebsocketAudioEndpoint"));
    if (BaseUrl.IsEmpty())
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("WebsocketAudioBaseUrl not configured (set AudioBaseUrl or [%s] WebsocketAudioBaseUrl in DefaultGame.ini)"),
            ConfigSection.IsEmpty() ? FApp::GetProjectName() : *ConfigSection);
        return;
    }

    const FString Url = BuildWsUrl(BaseUrl, AccessToken, AgentId, TokenQueryParam, AgentQueryParam);

    CloseAudioWebSocket();
    {
        FScopeLock Lock(&AudioWsSendCS);
        AudioWebSocket = FWebSocketsModule::Get().CreateWebSocket(Url, FString());
    }
    AudioWebSocket->OnConnected().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnAudioConnected);
    AudioWebSocket->OnConnectionError().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnAudioConnectionError);
    AudioWebSocket->OnClosed().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnAudioClosed);
    AudioWebSocket->OnMessage().AddUObject(this, &UNidalheimVoiceTurnPipelineComponent::OnAudioMessageReceived);

    LogResolvedAddress(BaseUrl);
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Connecting audio WebSocket (auth'd) to base: %s"), *BaseUrl);
    AudioWebSocket->Connect();
}

void UNidalheimVoiceTurnPipelineComponent::CloseAudioWebSocket()
{
    FScopeLock Lock(&AudioWsSendCS);
    if (!AudioWebSocket) return;
    // A late close/message from the old socket must not reset a newer agent's audio.
    AudioWebSocket->OnConnected().RemoveAll(this);
    AudioWebSocket->OnConnectionError().RemoveAll(this);
    AudioWebSocket->OnClosed().RemoveAll(this);
    AudioWebSocket->OnMessage().RemoveAll(this);
    AudioWebSocket->Close();
    AudioWebSocket.Reset();
}

void UNidalheimVoiceTurnPipelineComponent::RequestReconnect(bool bTextChannel)
{
    if (bShuttingDown) return;

    TWeakObjectPtr<UNidalheimVoiceTurnPipelineComponent> WeakSelf(this);
    RequestToken(FNidalheimVoiceTurnTokenReady::CreateLambda(
        [WeakSelf, bTextChannel](bool bSuccess, const FString& Token)
        {
            UNidalheimVoiceTurnPipelineComponent* Self = WeakSelf.Get();
            if (!Self || Self->bShuttingDown) return;
            if (!bSuccess || Token.IsEmpty())
            {
                UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Reconnect aborted - no access token"));
                return;
            }
            if (bTextChannel) Self->OpenTextSocketWithToken(Token);
            else Self->OpenAudioSocketWithToken(Token);
        }));
}

void UNidalheimVoiceTurnPipelineComponent::ScheduleTextReconnect()
{
    if (bShuttingDown)
    {
        return;
    }
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    // Single flight: one pending attempt at a time (close + error can fire together).
    if (World->GetTimerManager().IsTimerActive(TextReconnectTimer))
    {
        return;
    }
    World->GetTimerManager().SetTimer(TextReconnectTimer, this,
        &UNidalheimVoiceTurnPipelineComponent::ConnectTextWebSocket, TextReconnectDelaySeconds, /*bLoop*/ false);
}

void UNidalheimVoiceTurnPipelineComponent::SendRawJson(const FString& Json)
{
    if (TrySendRawJson(Json))
    {
        return;
    }
    // Text channel down: keep the message, it is sent on the next OnTextConnected.
    PendingTextSends.Add(Json);
    ScheduleTextReconnect();
}

bool UNidalheimVoiceTurnPipelineComponent::TrySendRawJson(const FString& Json)
{
    if (!IsTextChannelConnected())
    {
        return false;
    }
    TextWebSocket->Send(Json);
    return true;
}

void UNidalheimVoiceTurnPipelineComponent::FlushPendingTextSends()
{
    if (PendingTextSends.Num() == 0 || !IsTextChannelConnected())
    {
        return;
    }
    for (const FString& Message : PendingTextSends)
    {
        TextWebSocket->Send(Message);
    }
    PendingTextSends.Reset();
}

bool UNidalheimVoiceTurnPipelineComponent::IsTextChannelConnected() const
{
    return TextWebSocket.IsValid() && TextWebSocket->IsConnected();
}

void UNidalheimVoiceTurnPipelineComponent::SetAgentId(const FString& NewAgentId)
{
    if (NewAgentId.IsEmpty() || NewAgentId == AgentId)
    {
        return;
    }

    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Switching agent: %s -> %s"), *AgentId, *NewAgentId);
    AgentId = NewAgentId;
    ResetSpeechPlayback();

    if (TextWebSocket.IsValid())
    {
        TextWebSocket->Close();
        TextWebSocket.Reset();
    }
    CloseAudioWebSocket();

    // Text: the reconnect loop reopens with the new agent id. Audio: a single reconnection.
    bAudioReconnectAttempted = false;
    ScheduleTextReconnect();
    RequestReconnect(false);
}

void UNidalheimVoiceTurnPipelineComponent::SendMessage(const FString& Message)
{
    // NEVER queued: a player's text replayed after a reconnect would arrive out of context
    // (a ghost answer several seconds later). Connection down -> the message is lost and the
    // player retries; only technical messages (sync, clear...) are queued.
    if (TrySendRawJson(Message))
    {
        return;
    }
    UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("User message dropped (text WebSocket down), reconnecting"));
    ScheduleTextReconnect();
}

void UNidalheimVoiceTurnPipelineComponent::SendClearHistory()
{
    SendRawJson(TEXT("{\"type\":\"clear_history\"}"));
}

// Free-function callback invoked by miniaudio's internal audio thread for each captured chunk.
// We send directly from this thread - IWebSocket queues sends in a thread-safe internal buffer,
// and an FCriticalSection around the Send serialises with COMMIT (game thread). This avoids an
// AsyncTask -> game-thread hop which, at 50+ dispatches/sec, was the main suspect for a
// multi-second backlog observed server-side.
static void NidalheimMiniaudioCaptureCallback(ma_device* pDevice, void* /*pOutput*/, const void* pInput, ma_uint32 frameCount)
{
    if (!pDevice || !pInput || frameCount == 0) return;
    UNidalheimVoiceTurnPipelineComponent* Owner = static_cast<UNidalheimVoiceTurnPipelineComponent*>(pDevice->pUserData);
    if (!Owner) return;

    const SIZE_T NumBytes = static_cast<SIZE_T>(frameCount) * sizeof(int16);
    TArray<uint8> Raw;
    Raw.Append(static_cast<const uint8*>(pInput), static_cast<int32>(NumBytes));
    FString Base64 = FBase64::Encode(Raw);

    Owner->SendAudioBase64(Base64);
}

void UNidalheimVoiceTurnPipelineComponent::StartAudioCapture()
{
    if (bIsCapturingAudio)
    {
        return;
    }

    if (!AudioWebSocket.IsValid() || !AudioWebSocket->IsConnected())
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("Audio WebSocket not connected, cannot start capture"));
        return;
    }

    if (MiniAudioDevice)
    {
        // Stale device from a previous capture cycle that didn't clean up.
        ma_device* Stale = static_cast<ma_device*>(MiniAudioDevice);
        ma_device_uninit(Stale);
        FMemory::Free(MiniAudioDevice);
        MiniAudioDevice = nullptr;
    }

    const ma_uint32 CaptureRate = static_cast<ma_uint32>(FMath::Clamp(CaptureSampleRate, 8000, 96000));

    ma_device_config Config = ma_device_config_init(ma_device_type_capture);
    Config.capture.format = ma_format_s16;
    Config.capture.channels = kCaptureChannels;
    Config.sampleRate = CaptureRate;
    Config.dataCallback = &NidalheimMiniaudioCaptureCallback;
    Config.pUserData = this;

    // Tell miniaudio we care about latency, not battery. Affects internal
    // defaults; redundant with our explicit period sizing but cheap insurance.
    Config.performanceProfile = ma_performance_profile_low_latency;

    // 20 ms periods x 2 = ~40 ms audio-thread budget. WASAPI exclusive adds
    // ~3-5 ms below that, total capture latency lands ~25-30 ms. We tried
    // 5 ms periods first but that put 200 sends/sec into UE's WebSocket
    // pipeline, which appears to backlog under PIE load (multi-second
    // chunk arrival lag observed on server). 50 sends/sec at 20 ms periods
    // is the same throughput UAudioCaptureComponent used to push at, and
    // gives us margin while keeping latency well under perceptual threshold.
    Config.periodSizeInFrames = CaptureRate / 50;
    Config.periods = 2;

    // Try exclusive first for minimum latency. If the mic doesn't support it
    // (some HFP Bluetooth, some virtual devices) or another app already holds
    // exclusive, fall back to shared so capture still works. While in
    // exclusive, no other app (Discord, Teams, browser) can use the mic.
    Config.capture.shareMode = ma_share_mode_exclusive;

    // WASAPI-specific tuning. usage=games hints Avrt API to put the audio
    // thread on the "Games" task scheduling class (real-time priority,
    // resists priority inversion). noAutoStreamRouting prevents the device
    // from being silently rebuilt by the OS on default-device changes -
    // we manage that ourselves on push-to-talk start. noHardwareOffloading
    // disables WASAPI's hardware offload path, which on some sound cards
    // adds an extra buffering layer.
    Config.wasapi.usage = ma_wasapi_usage_games;
    Config.wasapi.noAutoStreamRouting = MA_TRUE;
    Config.wasapi.noHardwareOffloading = MA_TRUE;

    void* DeviceMem = FMemory::Malloc(sizeof(ma_device));
    if (!DeviceMem)
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio: failed to allocate device storage"));
        return;
    }

    ma_device* Device = static_cast<ma_device*>(DeviceMem);
    bool bExclusiveMode = true;
    ma_result InitResult = ma_device_init(NULL, &Config, Device);
    if (InitResult != MA_SUCCESS)
    {
        UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("miniaudio: WASAPI exclusive mode failed (result=%d), falling back to shared"),
            static_cast<int32>(InitResult));
        Config.capture.shareMode = ma_share_mode_shared;
        InitResult = ma_device_init(NULL, &Config, Device);
        bExclusiveMode = false;
    }

    if (InitResult != MA_SUCCESS)
    {
        FMemory::Free(DeviceMem);
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio: ma_device_init failed in both exclusive and shared (result=%d)"),
            static_cast<int32>(InitResult));
        return;
    }

    if (ma_device_start(Device) != MA_SUCCESS)
    {
        ma_device_uninit(Device);
        FMemory::Free(DeviceMem);
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio: ma_device_start failed"));
        return;
    }

    MiniAudioDevice = DeviceMem;
    bIsCapturingAudio = true;
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("miniaudio capture started (WASAPI %s, %u Hz mono PCM16, period=%u frames)"),
        bExclusiveMode ? TEXT("exclusive") : TEXT("shared"),
        CaptureRate, Config.periodSizeInFrames);
}

void UNidalheimVoiceTurnPipelineComponent::StopAudioCapture()
{
    if (!bIsCapturingAudio && !MiniAudioDevice)
    {
        return;
    }

    if (MiniAudioDevice)
    {
        ma_device* Device = static_cast<ma_device*>(MiniAudioDevice);
        ma_device_stop(Device);
        ma_device_uninit(Device);
        FMemory::Free(MiniAudioDevice);
        MiniAudioDevice = nullptr;
    }

    bIsCapturingAudio = false;
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("miniaudio capture stopped"));
}

void UNidalheimVoiceTurnPipelineComponent::CommitAudioBuffer()
{
    FScopeLock Lock(&AudioWsSendCS);
    if (!AudioWebSocket.IsValid() || !AudioWebSocket->IsConnected())
    {
        UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Cannot commit audio buffer - WebSocket not connected"));
        return;
    }

    AudioWebSocket->Send(TEXT("COMMIT"));
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Audio buffer committed - requesting AI response"));
}

void UNidalheimVoiceTurnPipelineComponent::SendAudioBase64(const FString& Base64)
{
    FScopeLock Lock(&AudioWsSendCS);
    if (!bIsCapturingAudio || !AudioWebSocket.IsValid() || !AudioWebSocket->IsConnected())
    {
        return;
    }

    AudioWebSocket->Send(Base64);
}

void UNidalheimVoiceTurnPipelineComponent::OnTextConnected()
{
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Text WebSocket connected"));

    // Everything requested while the socket was down goes out now (clear_history, syncs...).
    FlushPendingTextSends();

    // The host pushes its own state snapshots from here.
    OnTextChannelConnected.Broadcast();
}

void UNidalheimVoiceTurnPipelineComponent::OnTextConnectionError(const FString& Error)
{
    UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Text WebSocket error: %s - retrying"), *Error);
    TextWebSocket.Reset();
    ScheduleTextReconnect();
}

void UNidalheimVoiceTurnPipelineComponent::OnTextClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Text WebSocket closed: %d - %s (clean=%d) - retrying"), StatusCode, *Reason, bWasClean ? 1 : 0);

    TextWebSocket.Reset();
    ScheduleTextReconnect();
}

void UNidalheimVoiceTurnPipelineComponent::OnTextMessageReceived(const FString& Message)
{
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Text message received: %s"), *Message);

    // A typed JSON server event is forwarded to the host and never shown as dialogue.
    if (TryForwardTypedEvent(ENidalheimVoiceTurnChannel::Text, Message)) return;

    OnMessageReceived.Broadcast(Message);
}

bool UNidalheimVoiceTurnPipelineComponent::TryForwardTypedEvent(ENidalheimVoiceTurnChannel Channel, const FString& Message)
{
    // Server events arrive as JSON ({"type":...}); a normal reply on the text channel is plain text.
    if (!Message.StartsWith(TEXT("{"))) return false;

    TSharedPtr<FJsonObject> Json;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid()) return false;

    FString Type;
    if (!Json->TryGetStringField(TEXT("type"), Type)) return false;

    OnUnhandledServerEvent.Broadcast(Channel, Type, Message);
    return true;
}

void UNidalheimVoiceTurnPipelineComponent::OnAudioConnected()
{
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Audio WebSocket connected - ready for push-to-talk"));
    bAudioReconnectAttempted = false;

    EnsureMiniAudioPlaybackStarted();
    ResetSpeechPlayback();

    // The backend opens its speech-to-text at the rate declared here. Without this it stays in
    // "STT waiting for audio_config" and our PCM chunks pile up in a pre-config buffer that never
    // gets flushed: no transcript, no LLM, no TTS reply. The rate matches what miniaudio is
    // configured to emit; miniaudio resamples internally from whatever the device's native rate
    // is, so this declaration stays valid regardless of the user's mic.
    if (AudioWebSocket.IsValid() && AudioWebSocket->IsConnected())
    {
        const int32 CaptureRate = FMath::Clamp(CaptureSampleRate, 8000, 96000);

        const TSharedRef<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
        ConfigJson->SetStringField(TEXT("type"), TEXT("audio_config"));
        ConfigJson->SetNumberField(TEXT("sample_rate"), CaptureRate);
        ConfigJson->SetStringField(TEXT("device"), TEXT("miniaudio WASAPI"));
        ConfigJson->SetBoolField(TEXT("bluetooth_hfp"), false);

        FString ConfigStr;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ConfigStr);
        FJsonSerializer::Serialize(ConfigJson, Writer);
        {
            FScopeLock Lock(&AudioWsSendCS);
            AudioWebSocket->Send(ConfigStr);
        }

        UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Sent audio_config: rate=%d"), CaptureRate);
    }
}

void UNidalheimVoiceTurnPipelineComponent::OnAudioConnectionError(const FString& Error)
{
    ResetSpeechPlayback();
    UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("Audio WebSocket error: %s"), *Error);
}

void UNidalheimVoiceTurnPipelineComponent::OnAudioClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    ResetSpeechPlayback();
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Audio WebSocket closed: %d - %s (clean=%d)"), StatusCode, *Reason, bWasClean ? 1 : 0);

    if (bShuttingDown || bAudioReconnectAttempted) return;
    bAudioReconnectAttempted = true;

    CloseAudioWebSocket();
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("Attempting audio WebSocket reconnect after close"));
    RequestReconnect(false);
}

void UNidalheimVoiceTurnPipelineComponent::OnAudioMessageReceived(const FString& Message)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT("Failed to parse audio message JSON"));
        return;
    }

    FString Type;
    if (!JsonObject->TryGetStringField(TEXT("type"), Type))
    {
        return;
    }

    if (Type == TEXT("text"))
    {
        FString TextData;
        if (JsonObject->TryGetStringField(TEXT("data"), TextData))
        {
            UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT(">>> NPC RESPONSE: %s"), *TextData);
            OnMessageReceived.Broadcast(TextData);
        }
    }
    else if (Type == TEXT("error"))
    {
        FString ErrorMsg;
        if (JsonObject->TryGetStringField(TEXT("message"), ErrorMsg))
        {
            UE_LOG(LogNidalheimVoiceTurn, Error, TEXT(">>> SERVER ERROR: %s"), *ErrorMsg);
        }
    }
    else if (Type == TEXT("user_transcript"))
    {
        FString TranscriptData;
        if (JsonObject->TryGetStringField(TEXT("data"), TranscriptData))
        {
            UE_LOG(LogNidalheimVoiceTurn, Warning, TEXT(">>> USER TRANSCRIPT: %s"), *TranscriptData);
            OnUserTranscription.Broadcast(TranscriptData);
        }
    }
    else if (Type == TEXT("user_transcript_partial"))
    {
        // Live interim transcript: part of the protocol, not surfaced (nor forwarded to the host).
    }
    else if (Type == TEXT("audio_start"))
    {
        FString RequestId, Encoding;
        double SampleRate = 0, Channels = 0;
        const bool bValid = JsonObject->TryGetStringField(TEXT("request_id"), RequestId) && !RequestId.IsEmpty()
            && JsonObject->TryGetNumberField(TEXT("sample_rate"), SampleRate) && SampleRate == kPlaybackSampleRate
            && JsonObject->TryGetNumberField(TEXT("channels"), Channels) && Channels == kPlaybackChannels
            && JsonObject->TryGetStringField(TEXT("encoding"), Encoding) && Encoding == TEXT("pcm_s16le");
        ActiveAudioRequestId = RequestId;
        bRejectAudioReply = !bValid;
        if (!bValid)
        {
            UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("Rejected TTS response with invalid PCM format or request id"));
            return;
        }
        if (SpeechPlaybackBuffer) SpeechPlaybackBuffer->BeginSpeech();
    }
    else if (Type == TEXT("audio_end"))
    {
        FString RequestId;
        if (JsonObject->TryGetStringField(TEXT("request_id"), RequestId) && RequestId == ActiveAudioRequestId)
        {
            if (SpeechPlaybackBuffer) SpeechPlaybackBuffer->EndSpeech();
            ActiveAudioRequestId.Reset();
            bRejectAudioReply = false;
        }
    }
    else if (Type == TEXT("audio"))
    {
        FString AudioBase64;
        if (!JsonObject->TryGetStringField(TEXT("data"), AudioBase64)) return;

        FString RequestId;
        JsonObject->TryGetStringField(TEXT("request_id"), RequestId);
        if (bRejectAudioReply || RequestId != ActiveAudioRequestId || !SpeechPlaybackBuffer) return;

        TArray<uint8> AudioData;
        // Bound decoding too, not only the queue. Old servers without request IDs remain supported.
        const int64 MaxBase64Length = FNidalheimSpeechPlaybackBuffer::MaxQueuedFrames * sizeof(int16) * 4 / 3 + 4;
        if (AudioBase64.Len() > MaxBase64Length || !FBase64::Decode(AudioBase64, AudioData) ||
            !SpeechPlaybackBuffer->Enqueue(MoveTemp(AudioData), FPlatformTime::Seconds()))
        {
            bRejectAudioReply = true;
            SpeechPlaybackBuffer->Reset();
            UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("TTS response cancelled: invalid PCM or playback queue limit exceeded (120 s / 2048 chunks)"));
        }
    }
    else
    {
        // Anything else is the host game's business (the backend sends the same typed events on both channels).
        OnUnhandledServerEvent.Broadcast(ENidalheimVoiceTurnChannel::Audio, Type, Message);
    }
}

// What the playback callback needs. Outlives the device; no UObject access on the audio thread.
struct FNidalheimPlaybackContext
{
    FNidalheimSpeechPlaybackBuffer* Buffer = nullptr;
    FNidalheimVoiceSpatializer Spatializer;
};

static void NidalheimMiniaudioPlaybackCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
{
    if (!pDevice || !pOutput || frameCount == 0) return;
    auto* Context = static_cast<FNidalheimPlaybackContext*>(pDevice->pUserData);
    if (!Context || !Context->Buffer)
    {
        FMemory::Memzero(pOutput, SIZE_T(frameCount) * kOutputChannels * sizeof(int16));
        return;
    }

    // Render the mono voice into a fixed scratch block (no allocation on the audio thread), then
    // place it in the stereo field. Blocks are ~120 frames; the loop only guards bigger periods.
    constexpr ma_uint32 ScratchFrames = 1024;
    int16 Scratch[ScratchFrames];
    int16* Out = static_cast<int16*>(pOutput);
    const double Now = FPlatformTime::Seconds();
    for (ma_uint32 Done = 0; Done < frameCount;)
    {
        const ma_uint32 Count = FMath::Min(frameCount - Done, ScratchFrames);
        Context->Buffer->Render(Scratch, Count, Now);
        Context->Spatializer.MixToStereo(Scratch, Out + SIZE_T(Done) * kOutputChannels, Count);
        Done += Count;
    }
}

void UNidalheimVoiceTurnPipelineComponent::EnsureMiniAudioPlaybackStarted()
{
    if (MiniAudioPlaybackDevice && SpeechPlaybackBuffer) return;

    // Make sure we don't leak a half-initialised state from a previous attempt.
    TearDownMiniAudioPlayback();

    SpeechPlaybackBuffer = new FNidalheimSpeechPlaybackBuffer();
    PlaybackContext = new FNidalheimPlaybackContext();
    PlaybackContext->Buffer = SpeechPlaybackBuffer;

    ma_device_config Config = ma_device_config_init(ma_device_type_playback);
    Config.playback.format = ma_format_s16;
    Config.playback.channels = kOutputChannels;
    Config.sampleRate = kPlaybackSampleRate;
    Config.dataCallback = &NidalheimMiniaudioPlaybackCallback;
    Config.pUserData = PlaybackContext;
    Config.performanceProfile = ma_performance_profile_low_latency;

    // 5 ms periods x 2 = ~10 ms playback-thread budget. WASAPI shared layer
    // typically adds ~10 ms on top, so total render latency lands ~20 ms (vs
    // 100-500 ms for USoundWaveProcedural + AudioMixer).
    Config.periodSizeInFrames = kPlaybackSampleRate / 200;
    Config.periods = 2;

    // Shared mode for playback so other apps (Discord, Windows alerts, etc.)
    // can still play sound through the speakers while the game is running.
    // Exclusive output would silence everything else, which is too disruptive
    // for a chat-with-an-agent use case.
    Config.playback.shareMode = ma_share_mode_shared;
    Config.wasapi.usage = ma_wasapi_usage_games;
    Config.wasapi.noAutoStreamRouting = MA_TRUE;
    Config.wasapi.noHardwareOffloading = MA_TRUE;

    void* DeviceMem = FMemory::Malloc(sizeof(ma_device));
    if (!DeviceMem)
    {
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio playback: device alloc failed"));
        TearDownMiniAudioPlayback();
        return;
    }
    ma_device* Device = static_cast<ma_device*>(DeviceMem);

    if (ma_device_init(NULL, &Config, Device) != MA_SUCCESS)
    {
        FMemory::Free(DeviceMem);
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio playback: ma_device_init failed"));
        TearDownMiniAudioPlayback();
        return;
    }

    if (ma_device_start(Device) != MA_SUCCESS)
    {
        ma_device_uninit(Device);
        FMemory::Free(DeviceMem);
        UE_LOG(LogNidalheimVoiceTurn, Error, TEXT("miniaudio playback: ma_device_start failed"));
        TearDownMiniAudioPlayback();
        return;
    }

    MiniAudioPlaybackDevice = DeviceMem;
    // The device is born at full volume: re-apply the player's voice setting immediately, otherwise
    // a device recreated mid-game would ignore the menu slider.
    ApplyVoicePlaybackVolume();
    UpdateSpatialization();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(SpeechStatsTimer, this, &UNidalheimVoiceTurnPipelineComponent::LogSpeechPlaybackStats, 1.0f, true);
        World->GetTimerManager().SetTimer(SpatialTimer, this, &UNidalheimVoiceTurnPipelineComponent::UpdateSpatialization, kSpatialUpdateSeconds, true);
    }
    UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("miniaudio playback started (WASAPI shared, %u Hz mono PCM16 voice on a stereo device, period=%u frames)"),
        kPlaybackSampleRate, Config.periodSizeInFrames);
}

void UNidalheimVoiceTurnPipelineComponent::UpdateSpatialization()
{
    if (!PlaybackContext) return;

    float Gain = 1.f;
    float Pan = 0.f;

    UWorld* World = GetWorld();
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (bSpatialize && Controller)
    {
        FVector Source = FVector::ZeroVector;
        bool bHasSource = false;
        if (SourceLocationProvider.IsBound()) bHasSource = SourceLocationProvider.Execute(Source);
        else if (const AActor* Owner = GetOwner()) { Source = Owner->GetActorLocation(); bHasSource = true; }

        if (bHasSource)
        {
            FVector ListenerLocation;
            FRotator ListenerRotation;
            Controller->GetPlayerViewPoint(ListenerLocation, ListenerRotation);
            const FVector ListenerRight = FRotationMatrix(ListenerRotation).GetScaledAxis(EAxis::Y);
            FNidalheimVoiceSpatializer::Compute(Source, ListenerLocation, ListenerRight,
                SpatialMinDistance, SpatialMaxDistance, SpatialRolloff, Gain, Pan);
        }
    }
    PlaybackContext->Spatializer.SetTarget(Gain, Pan);
}

void UNidalheimVoiceTurnPipelineComponent::SetVoicePlaybackVolume(float Volume)
{
    GVoicePlaybackVolume = FMath::Clamp(Volume, 0.f, 1.f);

    // Applies to pipelines already speaking. There are at most a handful (one per player), so the
    // iteration is negligible next to a slider drag.
    for (TObjectIterator<UNidalheimVoiceTurnPipelineComponent> It; It; ++It)
    {
        if (IsValid(*It))
        {
            It->ApplyVoicePlaybackVolume();
        }
    }
}

void UNidalheimVoiceTurnPipelineComponent::ApplyVoicePlaybackVolume() const
{
    if (MiniAudioPlaybackDevice)
    {
        ma_device_set_master_volume(static_cast<ma_device*>(MiniAudioPlaybackDevice), GVoicePlaybackVolume);
    }
}

void UNidalheimVoiceTurnPipelineComponent::TearDownMiniAudioPlayback()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(SpeechStatsTimer);
        World->GetTimerManager().ClearTimer(SpatialTimer);
    }
    if (MiniAudioPlaybackDevice)
    {
        ma_device* Device = static_cast<ma_device*>(MiniAudioPlaybackDevice);
        ma_device_stop(Device);
        ma_device_uninit(Device);
        FMemory::Free(MiniAudioPlaybackDevice);
        MiniAudioPlaybackDevice = nullptr;
    }
    // The device is stopped: neither the context nor the queue can be read any more.
    delete PlaybackContext;
    PlaybackContext = nullptr;
    delete SpeechPlaybackBuffer;
    SpeechPlaybackBuffer = nullptr;
    LastCompletedSpeechReplies = LastSpeechUnderruns = 0;
}

void UNidalheimVoiceTurnPipelineComponent::ResetSpeechPlayback()
{
    if (SpeechPlaybackBuffer) SpeechPlaybackBuffer->Reset();
    ActiveAudioRequestId.Reset();
    bRejectAudioReply = false;
}

void UNidalheimVoiceTurnPipelineComponent::LogSpeechPlaybackStats()
{
    if (!SpeechPlaybackBuffer) return;
    const auto Stats = SpeechPlaybackBuffer->GetStats();
    if (Stats.CompletedReplies != LastCompletedSpeechReplies || Stats.Underruns != LastSpeechUnderruns)
    {
        UE_LOG(LogNidalheimVoiceTurn, Log, TEXT("TTS playback: received=%lld played=%lld queued=%lld cancelled=%lld frames; underruns=%lld rebuffer=%.0fms completed=%lld"),
            Stats.ReceivedFrames, Stats.PlayedFrames, Stats.QueuedFrames, Stats.CancelledFrames,
            Stats.Underruns, Stats.RebufferFrames * 1000.0 / kPlaybackSampleRate, Stats.CompletedReplies);
        LastCompletedSpeechReplies = Stats.CompletedReplies;
        LastSpeechUnderruns = Stats.Underruns;
    }
}
