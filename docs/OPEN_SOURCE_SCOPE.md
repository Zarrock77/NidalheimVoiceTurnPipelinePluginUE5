# Open source scope

This page says what this repository opens, what it does not, and under which license.

## What this repository is

`NidalheimVoiceTurnPipeline` is an Unreal Engine 5 plugin: the client side of a voice conversation
with an AI agent. It owns the networking and the audio (two WebSocket channels, push-to-talk capture,
low-latency TTS playback) and leaves everything game-specific to the host: authentication through a
token provider delegate, and game events through `OnUnhandledServerEvent`. It was extracted from the
Nidalheim game so that it can be reused in other projects.

Its server-side counterpart is
[nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs);
both speak the same wire protocol, which the README documents.

## What is open

| In this repository (MIT) | Why |
|---|---|
| `UNidalheimVoiceTurnPipelineComponent` and the voice playback / spatialization code | Generic: an `UActorComponent` usable on any actor |
| The wire protocol client (documented in the README) | Needed to talk to a compatible backend |
| Automation tests | So changes can be reviewed and verified |
| `miniaudio`, vendored under `ThirdParty/` | Required for capture and playback; see below |

## What is not part of this repository

The Nidalheim game, its gameplay systems, its quest system, its character content and its server
infrastructure are closed-source and are not published here. This plugin does not depend on any of
them: authentication and game messages are injected by the host.

## License

MIT. See [LICENSE](../LICENSE).

In practice, MIT lets you use, modify, merge, publish, distribute, sublicense and sell the plugin's
source, including inside a commercial game. The conditions are that the copyright and license notice
stay with copies of the code, and that it is provided without warranty.

Using the plugin requires Unreal Engine, which is governed by Epic Games' own license terms. That
license is separate from this plugin's MIT license and is not changed by it.

## Third-party code

| Component | License | Where |
|---|---|---|
| [miniaudio](https://miniaud.io) 0.11.25, by David Reid | Public domain or MIT-0 (your choice, stated at the end of `miniaudio.h`) | `Source/NidalheimVoiceTurnPipeline/ThirdParty/miniaudio` |

Both of miniaudio's license options are compatible with this plugin's MIT license.

## Platforms

Windows (Win64) only. miniaudio is built with the WASAPI backend alone.

## Contributing and security

See [CONTRIBUTING.md](../CONTRIBUTING.md) and [SECURITY.md](../SECURITY.md).
