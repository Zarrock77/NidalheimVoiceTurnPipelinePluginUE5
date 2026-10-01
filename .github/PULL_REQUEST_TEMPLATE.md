## What this PR changes

<!-- One to three sentences. The what and why, not the how. -->

Linked issue: <!-- #12, or "none" for a minor change -->

## Type

- [ ] `feat` -- new capability
- [ ] `fix` -- bug fix
- [ ] `refactor` -- reorganization, no behavior change
- [ ] `docs` -- documentation only
- [ ] `chore` / `style`

## What was tested

<!-- Editor build result, PIE verification scenario, host project used if any. -->

- [ ] Editor build succeeded (Development, Win64 or your target platform)
- [ ] Tested in PIE / a host project
- [ ] Automation tests pass (`NidalheimVoiceTurnPipeline`)
- [ ] Secret scan is green

## Checklist

- [ ] **Public API changed** (the component's properties, delegates, functions) --
      `README.md` updated to match
- [ ] Composition over inheritance preserved: this doesn't require a host game's NPC
      character to inherit from a plugin-provided base class
- [ ] No secrets, no real API keys, no project-specific (Nidalheim) assumptions baked
      into the plugin code
