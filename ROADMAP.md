# Roadmap

What is planned, and what is open to contributions. The source of truth is the
[open issues](../../issues); this page only groups them.

## Planned

- Expose the voice level, and maybe visemes, for lip sync (#13).
- Choose the capture and playback device (#20).
- Run the automation tests in CI (#22).
- Support macOS and Linux (#21).

## Open to a first contribution

These are small and self-contained. Each is labelled
[`good first issue`](../../issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22).

- Use a neutral agent id instead of `olaf` (#17).
- Document every field of `audio_config` (#18).
- Add tests for `SendRawJson` and `TrySendRawJson` (#19).

## Not planned

- Anything specific to a single game: quests, inventory, authentication. The host application owns those.
- Changing the wire protocol in only one of the two repositories.
