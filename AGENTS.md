# Project maintenance

- Runtime implementation: C++ in `native/`. Native checks are in `native/tests.cpp` and `native/deepseek_tests.cpp`; integration checks are in `tests/`.
- Released version: 0.3.1. Preserve published tags and assets. Subsequent functional changes use a new version.
- Keep application branding consistent across `assets/app.svg`, PNG, ICO and embedded resources: white background, interlocked green and blue quota rings with gray remainder arcs.
- Validate UI changes with core/UI checks, relevant integration tests and rendered previews. Identify simulated data in screenshots.
- Run uninstall tests only in isolated fixtures; preserve real installations, settings and credentials.
- Keep user-facing documentation focused on implemented features, usage and version changes. Exclude conversational history and abandoned alternatives.
- Store useful continuation notes in ignored `codex_tmp/`; promotional assets belong in ignored `local-media/`.
