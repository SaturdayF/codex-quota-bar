# Project maintenance

- `v0.1.0` is the original frozen source baseline. Do not move that tag. The user subsequently explicitly requested a directory-selectable installer and smaller ZIP within the same 0.1.0 Release, authorizing republishing its download assets. Track this installer supplement with a separate source tag and preserve the original tag/history.
- After the explicitly authorized 0.1.0 installer supplement, subsequent unrelated changes belong to 0.2.0. Preserve both source tags; replace release downloads only at the user's explicit request.
- Runtime C# belongs in `src/`; verification code belongs in `tests/`. Keep temporary work and useful continuation notes under `codex_tmp/`, ignored by Git.
- Keep `assets/app.svg`, PNG, ICO and embedded application branding consistent. The approved icon is E: white background, interlocked large green ring and smaller blue ring with gray remainder arcs, without Q tail.
- Never test uninstallation by deleting the user's installed app. Use a disposable fixture and `-WhatIf`; preserve the installed runtime and settings unless the user explicitly asks to uninstall them.
- Verify all native UI or taskbar changes on available screens with `--self-test`, appropriate integration checks and rendered previews. Clearly distinguish simulated preview quotas from real account data.
