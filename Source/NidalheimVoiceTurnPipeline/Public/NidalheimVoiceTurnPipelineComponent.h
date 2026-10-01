#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HAL/CriticalSection.h"
#include "TimerManager.h"
#include "NidalheimVoiceTurnPipelineComponent.generated.h"

class IWebSocket;
class FNidalheimSpeechPlaybackBuffer;
struct FNidalheimPlaybackContext;

/** Which WebSocket a server event arrived on. */
UENUM(BlueprintType)
enum class ENidalheimVoiceTurnChannel : uint8
{
    Text,
    Audio,
};

/** A reply to display (plain text on the text channel, `{"type":"text"}` on the audio channel). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNidalheimVoiceTurnMessageSignature, const FString&, Message);

/** What the speech-to-text heard (`{"type":"user_transcript"}`, audio channel). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNidalheimVoiceTurnTranscriptSignature, const FString&, Transcription);

/** The text channel just (re)connected and its queued messages were flushed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNidalheimVoiceTurnConnectedSignature);

/**
 * A typed JSON server event the component does not understand itself, forwarded verbatim so the
 * host game can handle its own messages. `Type` is the value of the `type` field, `RawJson` the
 * full message.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNidalheimVoiceTurnServerEventSignature,
    ENidalheimVoiceTurnChannel, Channel, const FString&, Type, const FString&, RawJson);

/** Completion callback of a token request. */
DECLARE_DELEGATE_TwoParams(FNidalheimVoiceTurnTokenReady, bool /*bSuccess*/, const FString& /*AccessToken*/);

/**
 * "Give me a fresh access token." The host binds this to whatever auth system it uses; the
 * plugin has no dependency on any particular one. It must eventually call `OnReady` exactly once.
 */
DECLARE_DELEGATE_OneParam(FNidalheimVoiceTurnTokenProvider, FNidalheimVoiceTurnTokenReady /*OnReady*/);

/**
 * "Where is the voice coming from?" Fill `OutLocation` with the world position of the speaker and
 * return true, or return false when there is no sensible source right now (the voice is then
 * played centered at full volume). Called ~30 times per second on the game thread: keep it cheap.
 */
DECLARE_DELEGATE_RetVal_OneParam(bool, FNidalheimVoiceTurnSourceLocationProvider, FVector& /*OutLocation*/);

/**
 * Voice-turn client: a text WebSocket and an audio WebSocket to a voice-turn backend
 * (https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs), push-to-talk capture and
 * TTS playback through miniaudio (WASAPI, bypassing the engine AudioMixer for low latency), and
 * the generic wire protocol: `text`, `error`, `user_transcript`, `audio_start`, `audio`,
 * `audio_end` and the client's `audio_config` / `COMMIT`.
 *
 * It can live on any actor. Anything game-specific (quests, inventory, ...) stays in the host:
 * unrecognized typed server events come out of OnUnhandledServerEvent, and the host pushes its own
 * messages with SendRawJson / TrySendRawJson.
 *
 * Set TokenProvider before Connect() (or before BeginPlay when bAutoConnect is true).
 */
UCLASS(ClassGroup = (Voice), meta = (BlueprintSpawnableComponent))
class NIDALHEIMVOICETURNPIPELINE_API UNidalheimVoiceTurnPipelineComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNidalheimVoiceTurnPipelineComponent();

    /** Identifier of the agent on the backend, sent as a query parameter on both sockets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoiceTurn")
    FString AgentId = TEXT("default");

    /** Connect both channels from BeginPlay. Turn off to call Connect() yourself. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn")
    bool bAutoConnect = true;

    /**
     * Section of DefaultGame.ini holding `WebsocketTextBaseUrl` and `WebsocketAudioBaseUrl`
     * (legacy keys `WebsocketTextEndpoint` / `WebsocketAudioEndpoint` are also read).
     * Empty means the project name.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection")
    FString ConfigSection;

    /** Text channel URL. Takes precedence over the ini when not empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection")
    FString TextBaseUrl;

    /** Audio channel URL. Takes precedence over the ini when not empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection")
    FString AudioBaseUrl;

    /** Query parameter carrying the access token. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection")
    FString TokenQueryParam = TEXT("token");

    /** Query parameter carrying AgentId. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection")
    FString AgentQueryParam = TEXT("npc");

    /** Retry delay while the text WebSocket is not connected. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Connection", meta = (ClampMin = "0.1"))
    float TextReconnectDelaySeconds = 0.5f;

    /**
     * Microphone capture rate, declared to the backend in `audio_config` (Hz, mono PCM16). miniaudio
     * resamples from the device's native rate, so any value the backend's STT accepts works.
     * Playback is fixed at 24 kHz mono PCM16, which is what the backend announces in `audio_start`.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Audio", meta = (ClampMin = "8000", ClampMax = "96000"))
    int32 CaptureSampleRate = 48000;

    /**
     * Play the voice in 3D: attenuated with the distance between the speaker and the player's
     * view, and balanced left/right. Done inside the miniaudio playback path (no engine AudioMixer),
     * so it adds no latency. Off by default: the voice is then centered at full volume.
     * Not covered: occlusion, reverb, HRTF.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Spatialization")
    bool bSpatialize = false;

    /** Inside this distance (cm) the voice is at full volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Spatialization", meta = (ClampMin = "0", EditCondition = "bSpatialize"))
    float SpatialMinDistance = 200.f;

    /** Past this distance (cm) the voice is silent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Spatialization", meta = (ClampMin = "1", EditCondition = "bSpatialize"))
    float SpatialMaxDistance = 1500.f;

    /** Falloff shape between the two distances: 1 is linear, higher fades faster. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoiceTurn|Spatialization", meta = (ClampMin = "0.1", ClampMax = "4", EditCondition = "bSpatialize"))
    float SpatialRolloff = 1.5f;

    /**
     * Where the voice comes from. Unbound: the owning actor's location. Bind it when the pipeline
     * actor is not the speaker's body (a hidden "brain" actor driving several characters).
     */
    FNidalheimVoiceTurnSourceLocationProvider SourceLocationProvider;

    /** Host-provided token source. */
    FNidalheimVoiceTurnTokenProvider TokenProvider;

    UPROPERTY(BlueprintAssignable, Category = "VoiceTurn")
    FNidalheimVoiceTurnMessageSignature OnMessageReceived;

    UPROPERTY(BlueprintAssignable, Category = "VoiceTurn")
    FNidalheimVoiceTurnTranscriptSignature OnUserTranscription;

    UPROPERTY(BlueprintAssignable, Category = "VoiceTurn")
    FNidalheimVoiceTurnConnectedSignature OnTextChannelConnected;

    UPROPERTY(BlueprintAssignable, Category = "VoiceTurn")
    FNidalheimVoiceTurnServerEventSignature OnUnhandledServerEvent;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Open both channels (no-op for a channel that is already open or connecting). */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn")
    void Connect();

    /** Plain-text message to the agent. Never queued: a stale message replayed after a reconnect would be answered out of context. */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn")
    void SendMessage(const FString& Message);

    /** Raw text-channel message, kept and sent on the next (re)connection if the channel is down. */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn")
    void SendRawJson(const FString& Json);

    /** Raw text-channel message sent right now. Returns false, and sends nothing, if the channel is down. */
    bool TrySendRawJson(const FString& Json);

    /** Asks the backend to erase the player's whole conversation history. */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn")
    void SendClearHistory();

    UFUNCTION(BlueprintPure, Category = "VoiceTurn")
    bool IsTextChannelConnected() const;

    /** Talk to a different agent: closes both sockets and reopens them with the new id. No-op if unchanged. */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn")
    void SetAgentId(const FString& NewAgentId);

    UFUNCTION(BlueprintCallable, Category = "VoiceTurn|Audio")
    void StartAudioCapture();

    UFUNCTION(BlueprintCallable, Category = "VoiceTurn|Audio")
    void StopAudioCapture();

    /** End of utterance: tells the backend to transcribe what it received and answer. */
    UFUNCTION(BlueprintCallable, Category = "VoiceTurn|Audio")
    void CommitAudioBuffer();

    UFUNCTION(BlueprintPure, Category = "VoiceTurn|Audio")
    bool IsCapturingAudio() const { return bIsCapturingAudio; }

    /**
     * Voice playback volume (0..1), applied to the miniaudio device. TTS playback bypasses the
     * engine AudioMixer, so no SoundClass or master volume reaches it: a volume slider has to come
     * through here. Global to the player and re-applied to every playback device started later.
     */
    static void SetVoicePlaybackVolume(float Volume);

    /** Public only so the free-function miniaudio capture callback can reach it. Not for gameplay code. */
    void SendAudioBase64(const FString& Base64);

private:
    friend class FNidalheimVoiceTurnPipelineRoutingTest;

    TSharedPtr<IWebSocket> TextWebSocket;
    TSharedPtr<IWebSocket> AudioWebSocket;

    // Opaque heap-allocated miniaudio objects (miniaudio.h is not exposed here).
    // Capture device: init'd by StartAudioCapture, uninit'd by StopAudioCapture / EndPlay.
    // Playback owns queued PCM until consumed; destroy only after stopping the device.
    void* MiniAudioDevice = nullptr;
    void* MiniAudioPlaybackDevice = nullptr;
    FNidalheimSpeechPlaybackBuffer* SpeechPlaybackBuffer = nullptr;
    // What the playback callback reads (queue + spatializer). Freed after the device is stopped.
    FNidalheimPlaybackContext* PlaybackContext = nullptr;
    FTimerHandle SpatialTimer;
    FString ActiveAudioRequestId;
    bool bRejectAudioReply = false;
    FTimerHandle SpeechStatsTimer;
    int64 LastCompletedSpeechReplies = 0;
    int64 LastSpeechUnderruns = 0;

    // Serialises all writes to AudioWebSocket: the miniaudio capture callback runs on its own thread
    // and sends directly, so COMMIT (game thread) and audio chunks (audio thread) could otherwise
    // enqueue concurrently.
    FCriticalSection AudioWsSendCS;

    bool bIsCapturingAudio = false;
    bool bAudioReconnectAttempted = false;
    bool bShuttingDown = false;

    // Persistent text connection: retried every TextReconnectDelaySeconds while down, queued sends
    // flushed on (re)connection. bTextConnecting prevents two concurrent connection attempts.
    bool bTextConnecting = false;
    FTimerHandle TextReconnectTimer;
    TArray<FString> PendingTextSends;

    void ConnectTextWebSocket();
    void ConnectAudioWebSocket();
    void OpenTextSocketWithToken(const FString& AccessToken);
    void OpenAudioSocketWithToken(const FString& AccessToken);
    void CloseAudioWebSocket();
    void RequestReconnect(bool bTextChannel);
    void RequestToken(FNidalheimVoiceTurnTokenReady OnReady);
    FString ResolveBaseUrl(const FString& OverrideUrl, const TCHAR* Key, const TCHAR* LegacyKey) const;
    static FString BuildWsUrl(const FString& BaseUrl, const FString& AccessToken, const FString& InAgentId,
        const FString& TokenParam, const FString& AgentParam);

    void ScheduleTextReconnect();
    void FlushPendingTextSends();

    void OnTextConnected();
    void OnTextConnectionError(const FString& Error);
    void OnTextClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void OnTextMessageReceived(const FString& Message);

    void OnAudioConnected();
    void OnAudioConnectionError(const FString& Error);
    void OnAudioClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void OnAudioMessageReceived(const FString& Message);

    /** A typed JSON message ({"type":...}) goes to OnUnhandledServerEvent and is never shown as dialogue. */
    bool TryForwardTypedEvent(ENidalheimVoiceTurnChannel Channel, const FString& Message);

    // Sets up the PCM queue + playback device on first audio connection. Idempotent.
    void EnsureMiniAudioPlaybackStarted();
    void TearDownMiniAudioPlayback();
    void ApplyVoicePlaybackVolume() const;
    // Publishes gain/balance for the current speaker and listener positions (~30 Hz).
    void UpdateSpatialization();
    void ResetSpeechPlayback();
    void LogSpeechPlaybackStats();

    void LogResolvedAddress(const FString& WebSocketURL) const;
};
