# Project maintenance

- `v0.1.0` is the original frozen source baseline. Do not move that tag. The user subsequently explicitly requested a directory-selectable installer and smaller ZIP within the same 0.1.0 Release, authorizing republishing its download assets. Track this installer supplement with a separate source tag and preserve the original tag/history.
- After the explicitly authorized 0.1.0 installer supplement, subsequent changes use new version tags. Preserve both source tags; replace release downloads only at the user's explicit request.
- The current 0.3.0 revision runtime is native C++ in `native/`; core/UI checks are in `native/tests.cpp`, installer checks in `tests/NativeInstaller.Tests.ps1`. Legacy C# sources, tests, scripts and discarded icon concepts were removed in 0.2.1; recover them from the frozen historical tags when needed. Keep useful continuation notes under `codex_tmp/`, ignored by Git.
- Keep `assets/app.svg`, PNG, ICO and embedded application branding consistent. The approved icon is E: white background, interlocked large green ring and smaller blue ring with gray remainder arcs, without Q tail.
- Never test uninstallation by deleting the user's installed app. Use a disposable fixture and `-WhatIf`; preserve the installed runtime and settings unless the user explicitly asks to uninstall them.
- Verify all native UI or taskbar changes on available screens with `--self-test`, appropriate integration checks and rendered previews. Clearly distinguish simulated preview quotas from real account data.

- `v0.2.2` is frozen at `92f6a0bae931ddd16ab89876d27c81aa72113496`; preserve its tag and published assets. The user explicitly withdrew the 0.3.0 GitHub Release and its assets and reopened 0.3.0 for fixes. Keep the historical v0.3.0 source tag at 2a8e2fa1b9f0fd73ac66ebab8f0362f93e7083f1, do not move it. Current fixes remain unpublished; do not create or republish a Release until the user explicitly requests it.
