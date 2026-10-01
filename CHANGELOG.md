# Changelog

All notable changes to this plugin are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/).

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

[0.1.0]: https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5/releases/tag/v0.1.0
