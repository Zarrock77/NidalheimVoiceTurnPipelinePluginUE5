# Contributing

## Current status

This repo currently has **no plugin code** — it reserves the name, license, and
governance files ahead of the actual extraction work (see `README.md`). There isn't a
build or test workflow to run yet.

If you want to help before the extraction happens, the most useful contribution is
discussion: open an issue if you have thoughts on the planned shape (the
`UActorComponent` API, the server-event delegate, the auth token delegate — see
`README.md`). Check the [project board](https://github.com/users/Zarrock77/projects/9)
first — it might already be tracked.

## Once there's code

This will be a standard Unreal Engine 5 plugin:

- Clone into a UE5 project's `Plugins/` directory, or add as a submodule.
- Build via the project's `.sln` (Visual Studio) or the editor's own compile step.
- No automated test suite is planned beyond what's realistic for a UE5 plugin without
  a full project to run it in (likely a small host project for manual / PIE
  verification).

This section will be rewritten with real instructions once there's something to build.

## Secret scanning

```bash
git clone https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5.git
cd NidalheimVoiceTurnPipelinePluginUE5
./scripts/install-git-hooks.sh
```

Enables a pre-commit hook that scans staged changes for secrets with
[gitleaks](https://github.com/gitleaks/gitleaks) (falls back to Docker if the binary
isn't installed, warns instead of blocking if neither is available). Worth doing even
with no plugin code yet — it protects config and workflow files from day one. CI also
rescans the full history on every push/PR.

## Branches and commits

- Work off a feature branch, not `main` directly.
- Commit messages: a short, descriptive summary line; explain *why* in the body when
  it isn't obvious from the diff.

## Pull requests

- Keep them small and focused.
- Describe what changed and why; link any related issue.
- A maintainer reviews before merging.

## Contact

Open an issue, or reach out via GitHub ([@Zarrock77](https://github.com/Zarrock77)).
See [SECURITY.md](SECURITY.md) instead for vulnerability reports.
