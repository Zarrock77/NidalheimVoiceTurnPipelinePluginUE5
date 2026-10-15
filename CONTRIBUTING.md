# Contributing

## Working on the plugin

This is a standard Unreal Engine 5 plugin (Windows only, see `README.md`):

- Put the repo in a UE5 project's `Plugins/` folder (clone it there, or add it as a submodule) and
  enable `NidalheimVoiceTurnPipeline` in the `.uproject`.
- Build the project's editor target (Visual Studio, or `Build.bat <Project>Editor Win64 Development`).
- Run the automation tests from the editor (Session Frontend > Automation, filter `NidalheimVoiceTurnPipeline`)
  or headless:
  `UnrealEditor.exe <Project>.uproject -ExecCmds="Automation RunTests NidalheimVoiceTurnPipeline; Quit" -unattended -nullrhi`.
- There is no CI build: GitHub-hosted runners can't build against Unreal Engine. State in your pull request
  which engine version you built and tested with, and what you checked in PIE.
- A real round trip (microphone, backend, authentication) can't be automated: if your change touches audio
  capture/playback or the WebSocket handling, say how you verified it by hand.

The plugin must stay reusable outside the game it was extracted from: no assumption about a specific host
project, auth system or backend beyond the documented wire protocol.

## Secret scanning

```bash
git clone https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5.git
cd NidalheimVoiceTurnPipelinePluginUE5
./scripts/install-git-hooks.sh
```

Enables a pre-commit hook that scans staged changes for secrets with
[gitleaks](https://github.com/gitleaks/gitleaks) (falls back to Docker if the binary
isn't installed, warns instead of blocking if neither is available). CI also rescans the
full history on every push/PR.

## Branches and commits

- Work off a feature branch, not `main` directly — `main` is protected, a direct
  `git push` to it is rejected by GitHub, including for the maintainer.
- Commit messages: a short, descriptive summary line; explain *why* in the body when
  it isn't obvious from the diff.

## Pull requests

- Keep them small and focused.
- Describe what changed and why; link any related issue.
- Branch protection requires the `Scan git history` (gitleaks) check to pass and be
  up to date with `main` before a PR is mergeable (there is no CI build, see above). No human approval is required by GitHub
  (solo-maintainer project), but the maintainer may still comment or ask for changes
  before merging.

## Contact

Open an issue, or reach out via GitHub ([@Zarrock77](https://github.com/Zarrock77)).
See [SECURITY.md](SECURITY.md) instead for vulnerability reports.


## What we accept, and what we keep control of

**Welcome without asking first:** Bug fixes, tests, documentation and examples; audio device selection; new platforms; performance and latency improvements that keep the public API compatible.

**Please open an issue before writing code for:**

- a change to the wire protocol: both this repository and
  [nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs) implement it, so it has to stay compatible or be made in both;
- the license, the release process or the repository configuration;
- anything that ties the project to one specific game or backend.

**We will decline:** changes that add game-specific logic, secrets or credentials, or code you do not have
the right to contribute.

## How fast we reply

We aim to look at an issue or a pull request **within about a week**. This is a student team working
part-time: it is a best-effort aim, not a guarantee. If nobody has answered after two weeks, comment on the
thread to bring it back up.

## Credit

Every accepted contribution is credited in [CONTRIBUTORS.md](CONTRIBUTORS.md) and in the CHANGELOG entry of
the release that ships it.

## Governance and roadmap

See [GOVERNANCE.md](GOVERNANCE.md) for how decisions are made and releases cut, and [ROADMAP.md](ROADMAP.md)
for what is planned.
