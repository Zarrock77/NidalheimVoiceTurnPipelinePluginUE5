# Governance

This page says who decides what, how a contribution gets in, and what happens next.

## Who decides

- **Maintainer:** [@Zarrock77](https://github.com/Zarrock77). This is a student project with a single
  maintainer; there is no committee.
- **A contribution is accepted** when the maintainer approves the pull request and the required checks pass.
  Nobody merges their own change without a review comment from someone else when another reviewer is
  available.
- **A disagreement** is settled in the issue or pull request, in writing. If it is not resolved by
  discussion, the maintainer decides and writes the reason there, so it can be read later.
- **A change is declined** with a reason, never silently. A declined proposal can be reopened with new
  information.

## How a release is cut

- A release is a GitHub release with a tag `vX.Y.Z`. Before tagging, `Version` and `VersionName` in the
  `.uplugin` and the dated section of the CHANGELOG must say the same version.
- A release is worth cutting when a user-visible change has been merged on `main` (a feature or a fix).
  The maintainer decides when.
- The plugin is `0.x`: a change that breaks the public API (component properties, delegates) bumps the minor version and is listed under `Changed` in the CHANGELOG.

## Staying compatible with the other half

This repository and [nidalheim-voice-turn-pipeline](https://github.com/Zarrock77/NidalheimVoiceTurnPipelineBackendNodejs) implement the two ends of the same wire protocol. A change
to the protocol has to be compatible with the other repository, or be announced and made in both. This is
why protocol changes are discussed in an issue before any code is written.

## What happens after the EIP

This project was started as an Epitech EIP, a time-limited student project. No commitment to maintain it
beyond that has been made yet.

When the EIP ends, this section will be replaced by an honest status: **maintained** (by whom), **looking
for maintainers**, or **archived**, with what a newcomer needs to know to pick it up. Until then, the
maintainer reviews contributions on a best-effort basis.
