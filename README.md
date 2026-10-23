# NidalheimVoiceTurnPipelinePluginUE5

Unreal Engine 5 plugin (`NidalheimVoiceTurnPipeline`): the client side of a voice conversation
with an AI agent. It is the UE5 counterpart to
[nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs)
(the Node.js backend package) and speaks the same wire protocol.

It was extracted from `ANidalheimNPCCharacter` in the Nidalheim
game (a closed-source project) so that the voice client can be reused: it owns the networking and audio, and leaves the game
logic (quests, inventory, authentication...) to the host.

## What's in the box

`UNidalheimVoiceTurnPipelineComponent`, an `UActorComponent` you can put on any actor, that provides:

- **Two WebSocket channels**, `/text` and `/audio`, with automatic reconnection of the text channel and
  a send queue that survives reconnections.
- **Push-to-talk capture** through [miniaudio](https://miniaud.io) (WASAPI, ~25-30 ms): the microphone is
  streamed as base64 PCM16 mono, and `COMMIT` ends the utterance.
- **TTS playback** through miniaudio, bypassing the engine AudioMixer (~20 ms instead of 100-500 ms), with a
  jitter buffer that preserves reply order (`FNidalheimSpeechPlaybackBuffer`) and a player-level volume.
- **Optional 3D voice** (`bSpatialize`): distance attenuation and left/right balance computed inside that same
  playback path, so it adds no latency. No occlusion, reverb or HRTF: those would need the engine AudioMixer.
- **The generic wire protocol**: `text`, `error`, `user_transcript`, `audio_start`, `audio`, `audio_end`
  from the server; `audio_config` and `COMMIT` from the client.
- **A seam for the host game**: typed server events the plugin doesn't know are forwarded as-is, and the
  host pushes its own messages on the same text socket.
- **No auth dependency**: you hand it a token provider.

## Requirements

- Unreal Engine 5.8 (`EngineVersion` in the `.uplugin`; other versions are untested).
- **Windows only** (Win64): miniaudio is built with WASAPI alone.
- A backend implementing the protocol, such as the
  [Node.js package](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs).
- Some way to obtain an access token (a JWT, in the reference backend) and pass it as a query parameter.

## Install

1. Clone this repo into your project's `Plugins/` folder (any subfolder name works).
2. Enable the plugin in your `.uproject`:
   ```json
   { "Name": "NidalheimVoiceTurnPipeline", "Enabled": true }
   ```
3. Add the module to your game module's `Build.cs`:
   ```csharp
   PublicDependencyModuleNames.Add("NidalheimVoiceTurnPipeline");
   ```

## Quick start (C++)

Point both channels at your backend, in `Config/DefaultGame.ini` (the section is named after your project;
override it with `ConfigSection`, or skip the ini and set `TextBaseUrl` / `AudioBaseUrl` directly):

```ini
[MyProject]
WebsocketTextBaseUrl="wss://my-backend.example.com/text"
WebsocketAudioBaseUrl="wss://my-backend.example.com/audio"
```

Then on the actor that talks to the agent. The header first: the include and the `UFUNCTION` macros are
required, because `AddDynamic` only binds to `UFUNCTION`s.

```cpp
// MyActor.h
#include "GameFramework/Actor.h"
#include "NidalheimVoiceTurnPipelineComponent.h"
#include "MyActor.generated.h"

UCLASS()
class MYPROJECT_API AMyActor : public AActor
{
    GENERATED_BODY()
public:
    AMyActor();
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UNidalheimVoiceTurnPipelineComponent> Voice;

    UFUNCTION() void HandleReply(const FString& Message);
    UFUNCTION() void HandleTranscript(const FString& Transcription);
};
```

Then the implementation. `MyAuth` stands for **your own** authentication code, it is not part of the plugin:
all the plugin needs is a token handed to `OnReady`.

```cpp
// MyActor.cpp, constructor
Voice = CreateDefaultSubobject<UNidalheimVoiceTurnPipelineComponent>(TEXT("Voice"));
Voice->bAutoConnect = false;                // or leave it on and wire everything before BeginPlay

// MyActor.cpp, BeginPlay(), before Connect()
Voice->AgentId = TEXT("olaf");              // sent as ?npc=olaf (see AgentQueryParam)
Voice->TokenProvider.BindLambda([](FNidalheimVoiceTurnTokenReady OnReady)
{
    // Call OnReady exactly once, whenever your auth system has a fresh token.
    MyAuth->GetFreshToken([OnReady](bool bOk, const FString& Token) { OnReady.ExecuteIfBound(bOk, Token); });
});
Voice->OnMessageReceived.AddDynamic(this, &AMyActor::HandleReply);        // text to display
Voice->OnUserTranscription.AddDynamic(this, &AMyActor::HandleTranscript); // what the STT heard
Voice->Connect();

// Push-to-talk
Voice->StartAudioCapture();   // key pressed
Voice->StopAudioCapture();    // key released
Voice->CommitAudioBuffer();   //   ...then ask for the answer

// Typed chat
Voice->SendMessage(TEXT("Hello"));
```

Everything is also available to Blueprint (`BlueprintCallable` / `BlueprintAssignable`), except the native
`TokenProvider` delegate, which has to be bound from C++.

### Handling your game's own messages

The plugin only understands the generic protocol. Any other JSON message with a `type` field comes out of
`OnUnhandledServerEvent(Channel, Type, RawJson)`; `OnTextChannelConnected` fires after each (re)connection of
the text channel, which is the moment to push state snapshots. Send your own messages with:

- `SendRawJson(Json)`: queued if the channel is down, sent on reconnection;
- `TrySendRawJson(Json)`: sent now, returns `false` and sends nothing if the channel is down.

## Properties

| Property | Default | What it does |
|---|---|---|
| `AgentId` | `default` | Agent identifier sent on both sockets |
| `bAutoConnect` | `true` | Connect from `BeginPlay` |
| `ConfigSection` | project name | `DefaultGame.ini` section holding the URLs |
| `TextBaseUrl` / `AudioBaseUrl` | empty | Override the ini |
| `TokenQueryParam` / `AgentQueryParam` | `token` / `npc` | Query parameter names |
| `TextReconnectDelaySeconds` | `0.5` | Text channel retry delay |
| `CaptureSampleRate` | `48000` | Microphone rate declared in `audio_config` (Hz) |
| `bSpatialize` | `false` | Play the voice in 3D (off: centered, full volume) |
| `SpatialMinDistance` / `SpatialMaxDistance` | `200` / `1500` | Full volume inside the first (cm), silent past the second |
| `SpatialRolloff` | `1.5` | Falloff between the two: 1 is linear, higher fades faster |

Playback is fixed at 24 kHz mono PCM16, which is what the reference backend announces in `audio_start`.
The output device is stereo so the voice can be balanced.

### Where the voice comes from

With `bSpatialize` on, the voice comes from the owning actor's location and is heard from the first
player's view. When the component lives on an actor that is not the speaker's body (a hidden "brain"
actor driving several characters), bind `SourceLocationProvider` (called ~30 times per second on the
game thread). Return `false` when there is no source: the voice then plays centered at full volume.

```cpp
Voice->bSpatialize = true;
Voice->SourceLocationProvider.BindLambda([this](FVector& OutLocation)
{
    if (!SpeakerBody) return false;
    OutLocation = SpeakerBody->GetActorLocation();
    return true;
});
```

## Wire protocol

Client to server: `audio_config` (`{"type":"audio_config","sample_rate":48000,...}`, once per audio connection),
PCM16 base64 chunks while the key is held, the literal `COMMIT`, plain text messages on the text channel,
`{"type":"clear_history"}`. Server to client: `text`, `error`, `user_transcript`,
`user_transcript_partial` (ignored), `audio_start` / `audio` / `audio_end` (tagged with a `request_id`).
See the Node.js package for the server side.

## Tests

Automation tests live in the plugin (`NidalheimVoiceTurnPipeline.Routing`, `.Spatializer` and `.Speech.*`).
From a host project:

```
UnrealEditor.exe MyProject.uproject -ExecCmds="Automation RunTests NidalheimVoiceTurnPipeline; Quit" -unattended -nullrhi
```

They cover URL building, event routing, the token-provider contract, the playback buffer and the 3D mixing. A real
round trip (microphone, backend, login) can only be checked by hand in PIE.

## Scope

This plugin and the [nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs) backend package are the only parts of the Nidalheim game that are open source. They contain no game-specific
code and can be reused on their own. Everything else stays closed: the game itself, the quest system, the NPC data
(prompts, voices, model choices), the authentication plugin, and the backend services and infrastructure. That is
where the game's own value lies, and none of it is needed to use this plugin.

## Roadmap

What is planned and what is open to contributions: [ROADMAP.md](ROADMAP.md). Newcomers: look for the
[`good first issue`](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) label.
How decisions are made: [GOVERNANCE.md](GOVERNANCE.md).

## Third-party

[miniaudio](https://miniaud.io) by David Reid, vendored in `Source/NidalheimVoiceTurnPipeline/ThirdParty/miniaudio`
(public domain or MIT-0, see the license at the end of `miniaudio.h`).

## Contributing

**Just tried to install it?** Tell us how it went, even if it went well: open an issue with the
[Installation test report](../../issues/new?template=install-test.yml) form. What you had to guess is what the
documentation is missing.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Security

See [SECURITY.md](SECURITY.md) for how to report a vulnerability privately.

## License

MIT. See [LICENSE](LICENSE).
