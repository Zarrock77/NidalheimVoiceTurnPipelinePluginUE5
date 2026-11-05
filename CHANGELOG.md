# Changelog

All notable changes to this plugin are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- The audio channel now reconnects for good, like the text one: a failed token request, a connection error or an unexpected close schedules another attempt (it used to give up, or retry once).
- `Connect()` does nothing when the audio channel is already open or connecting, instead of opening a second socket.
- A late close or error from a replaced text socket (after `SetAgentId`) no longer resets the new one.

### Changed
- The send queue of the text channel keeps at most 64 messages (the oldest is dropped) and is emptied by `SetAgentId`, whose messages were meant for the previous agent.
- Reconnection uses an exponential backoff on both channels (`TextReconnectDelaySeconds`, then x2, capped at 30 s, reset on connection) instead of a fixed 0.5 s.
- Connecting no longer does a blocking DNS lookup just to log the resolved address; the `Sockets` and `JsonUtilities` module dependencies are dropped.

## [0.2.1] - 2026-10-23

### Added
- Plugin icon (`Resources/Icon128.png`) and install steps for Fab in the README.

### Changed
- The four team members are named as copyright holders in `LICENSE` and in the `.uplugin` `CreatedBy`.

## [0.2.0] - 2026-10-15

### Added
- 3D voice (`bSpatialize`, `SpatialMinDistance`, `SpatialMaxDistance`, `SpatialRolloff`): distance
  attenuation and left/right balance inside the miniaudio playback path, with no added latency.
  `SourceLocationProvider` tells the plugin where the speaker is when the owning actor is not its body.
  Off by default.

### Changed
- The playback device is now stereo (the TTS stream is still 24 kHz mono). A centered voice is exactly as
  loud as before.

## [0.1.0] - 2026-10-01

First public release (beta), extracted from Nidalheim's `ANidalheimNPCCharacter`.
Requires Unreal Engine 5.8, Windows (Win64) only.

### Added
- `UNidalheimVoiceTurnPipelineComponent`: text and audio WebSocket channels, push-to-talk
  capture and low-latency TTS playback (miniaudio, WASAPI), reconnection of the text channel.
- Pluggable authentication through a token provider delegate: no dependency on a specific
  auth system.
- `OnMessageReceived`, `OnUserTranscription`, `OnTextChannelConnected` and
  `OnUnhandledServerEvent` delegates so the host game handles its own server events.
- Automation tests (`NidalheimVoiceTurnPipeline.*`): URL building, typed-event routing,
  token provider behavior, playback jitter buffer.
- Open-source hygiene: MIT license, CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, issue and PR
  templates, gitleaks secret scanning, strict `main` branch protection.

### Known limitations
- TTS playback is fixed at 24 kHz mono; only the capture sample rate is configurable.
- Windows only (miniaudio is built with WASAPI only).
- Pairs with the Node.js backend package `nidalheim-voice-turn-pipeline`; a real
  end-to-end voice round-trip still has to be validated in a host project.

[0.2.1]: https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5/releases/tag/v0.2.1
[0.2.0]: https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5/releases/tag/v0.2.0
[0.1.0]: https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5/releases/tag/v0.1.0
