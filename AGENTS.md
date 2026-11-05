# Project maintenance

## Required reading and release descriptions

- At the start of every task in this project, read this `AGENTS.md` and apply its requirements before acting.
- Release descriptions must state the changes introduced in that version and summarize the features implemented in that version.
- Describe shipped behavior objectively, using the corresponding version's code and documentation as evidence. Do not present planned or unverified capabilities as implemented features.
- Exclude conversation history, development/testing narratives, temporary experiments, abandoned approaches, and comparisons with discarded solutions. Keep technical verification records in `codex_tmp/` rather than release descriptions.
- Keep local release-description files and GitHub release bodies consistent. When editing published descriptions, preserve existing tags, source commits, assets, release status, and latest/stable version selection unless the user explicitly requests otherwise.

## Maintenance rules

- Runtime implementation: C++ in `native/`. Native checks are in `native/tests.cpp` and `native/deepseek_tests.cpp`; integration checks are in `tests/`.
- Released version: 0.3.6. Preserve published tags and assets. Subsequent functional changes use a new version.
- Keep application branding consistent across `assets/app.svg`, PNG, ICO and embedded resources: white background, interlocked green and blue quota rings with gray remainder arcs.
- Validate UI changes with core/UI checks, relevant integration tests and rendered previews. Identify simulated data in screenshots.
- Run uninstall tests only in isolated fixtures; preserve real installations, settings and credentials.
- Keep user-facing documentation focused on implemented features, usage and version changes. Exclude conversational history and abandoned alternatives.
- Store useful continuation notes in ignored `codex_tmp/`; promotional assets belong in ignored `local-media/`.
