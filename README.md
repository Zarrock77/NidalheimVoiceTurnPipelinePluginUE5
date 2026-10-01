# NidalheimVoiceTurnPipelinePluginUE5

UE5 plugin counterpart to
[nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs)
(the Node.js backend package): a vendor- and transport-agnostic voice-turn
WebSocket client component — push-to-talk audio capture, playback, and the
generic text/audio WebSocket protocol — extracted from a UE5 game character
class, for reuse in other Unreal Engine 5 projects.

**Status: not implemented yet.** This repo is a placeholder reserving the
name and license ahead of the actual extraction work (ported from
`ANidalheimNPCCharacter` in the [Nidalheim](https://github.com/Zarrock77/Nidalheim)
client — see that repo's `client/CLAUDE.md` for the source this will be
extracted from). No `.uplugin`, no C++ source here yet.

Planned shape, subject to change once the extraction actually happens:

- A `UActorComponent` owning the two WebSocket connections (text/audio), the
  push-to-talk audio capture/playback pipeline, and the generic wire protocol
  (`text` / `error` / `user_transcript(_partial)` / `audio_start` / `audio` /
  `audio_end` / `audio_config`).
- A delegate for server events the component doesn't itself recognize, so a
  host game can handle its own business messages (quests, inventory, or
  anything else) without the plugin needing to know about them — the same
  seam as `onUtterance`/`onTurnComplete` in the Node.js package.
- A pluggable "give me a fresh token" delegate instead of a hard dependency
  on any specific auth system, so it isn't tied to RFC 8628 Device
  Authorization Grant specifically.
- Sample rates, reconnect timing, and base URLs as configurable properties
  instead of hardcoded constants.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Security

See [SECURITY.md](SECURITY.md) for how to report a vulnerability privately.

## License

MIT. See [LICENSE](LICENSE).
