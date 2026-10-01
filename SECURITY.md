# Security Policy

## Reporting a vulnerability

Please **do not** open a public issue for a security vulnerability.

Instead, use GitHub's private reporting:
[Report a vulnerability](https://github.com/Zarrock77/NidalheimVoiceTurnPipelinePluginUE5/security/advisories/new)
(Security tab → "Report a vulnerability"). This opens a private advisory visible only
to the maintainer until it's resolved.

If that isn't available to you, contact the maintainer directly via GitHub
([@Zarrock77](https://github.com/Zarrock77)).

## What to include

- A description of the vulnerability and its potential impact.
- Steps to reproduce, or a minimal proof of concept.
- The affected version/commit.
- Your assessment of severity, if you have one.

## Response

This is a small, part-time-maintained open source project — there is no SLA. As a
guideline: an initial acknowledgment within **7 days**, and a plan (fix, timeline, or
explanation) within **30 days** of a confirmed report. Credit is given to reporters in
the advisory/release notes unless you ask to stay anonymous.

## Sensitive areas

The plugin handles an access token (it appears in the WebSocket URL query string, as the backend
protocol requires) and streams microphone audio to the configured backend. Reports about token
handling, logging of sensitive values, or audio capture/transmission outside of an explicit
push-to-talk are especially welcome.

## Scope

This policy covers the code in this repository. It does not cover
vulnerabilities in Unreal Engine itself, third-party plugins, or the host game project
this plugin is extracted from ([Zarrock77/Nidalheim](https://github.com/Zarrock77/Nidalheim),
private).
