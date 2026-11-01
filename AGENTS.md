# Project maintenance

- `v0.1.0` is the frozen baseline. Do not amend, move, or replace a published tag or its release assets.
- Any subsequent product/code/documentation change belongs to the next version, initially `0.2.0`. Before changing it, bump `version.props`, update the release/version tests and CHANGELOG, and create a new release rather than replacing v0.1.0.
- Runtime C# belongs in `src/`; verification code belongs in `tests/`. Keep temporary work and useful continuation notes under `codex_tmp/`, ignored by Git.
- Keep `assets/app.svg`, PNG, ICO and embedded application branding consistent. The approved icon is E: white background, interlocked large green ring and smaller blue ring with gray remainder arcs, without Q tail.
- Never test uninstallation by deleting the user's installed app. Use a disposable fixture and `-WhatIf`; preserve the installed runtime and settings unless the user explicitly asks to uninstall them.
- Verify all native UI or taskbar changes on available screens with `--self-test`, appropriate integration checks and rendered previews. Clearly distinguish simulated preview quotas from real account data.
