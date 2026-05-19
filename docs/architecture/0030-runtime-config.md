# ADR 0030 — Runtime config: typed `AppConfig` for user-facing tunables

**Status:** Accepted
**Date:** 2026-05-19

## Context

By Phase 4 of the UI work, `app/src/app.cpp` had accumulated a cluster of
hardcoded literals that were intentional v0.x defaults but had nowhere to
live as the project matured:

- Window geometry (`1600 × 1000`, title `"noted"`).
- Frames-in-flight (`2`).
- Camera zoom step (`1.1`) and clamp range (`0.1 .. 32.0`).
- Font size (`16.0F px`) and the OS-probed CJK font path.
- The SPV shader directory (compile-time `NOTED_SHADER_DIR` only — no
  runtime override, no ability to ship the binary somewhere else).
- The default theme (`ThemeKind::dark`).

These weren't bugs — the values are reasonable — but they had three
problems:

1. **No user override.** A second monitor, a HiDPI display, a different
   trackpad zoom feel — every adjustment required a rebuild.
2. **Tied to source-tree layout.** `NOTED_SHADER_DIR` baked the absolute
   build-tree path into the executable, so the binary was not
   relocatable.
3. **No central place to look.** "Where is this configured?" had no
   answer; tunables were scattered across init methods.

## Decision

Introduce a single typed POD, `noted::app::config::AppConfig`, that owns
the user-facing knobs. Source-of-truth priority (highest wins):

1. **Per-field environment variables** (currently only
   `NOTED_SHADER_DIR` — others added as needed).
2. **`noted.config.json`** alongside the executable, parsed with
   `nlohmann::json` (`ignore_comments=true`, unknown keys ignored).
3. **`AppConfig::defaults()`** — the v0.x hardcoded values, preserved
   bit-for-bit so a default config produces identical behaviour.

`App::create(config::AppConfig cfg = AppConfig::defaults())` consumes
the snapshot at construction. Init steps read `cfg_.window.*`,
`cfg_.canvas.*`, `cfg_.font.*`, `cfg_.assets.*`, `cfg_.ui.*` directly.
Per-frame tunables (`zoom_step`, `zoom_min`, `zoom_max`) are re-read on
every event so a future Preferences UI can flip them live without
restart; one-shot tunables (window size, frames-in-flight) are baked at
startup.

Shader resolution is centralized in `resolve_shader_dir(cfg)` which
walks: explicit config override → `NOTED_SHADER_DIR` env →
`<exe_dir>/shaders` → compile-time `NOTED_SHADER_DIR` fallback. A
CMake post-build step copies the compiled `.spv` files next to the
binary, so `<exe_dir>/shaders` always works in production layouts. The
compile-time fallback keeps the source-tree dev workflow seamless.

A malformed config logs to stderr and falls through to `defaults()`
rather than refusing to launch — same principle as `probe_cjk_font()`
returning empty: degrade, don't die.

## Alternatives considered

- **`harness::Config` for everything.** Rejected for v0.x: harness is
  optimized for live-tweakable engine internals (FeatureFlags, counters),
  not user-facing settings that persist across sessions. The two will
  coexist — engine tunables stay in harness (Phase 2), user-facing
  settings live in `AppConfig`. See [ADR 0004](0004-harness.md).
- **TOML / YAML / INI.** Rejected: JSON is already a dependency
  (`.noted` archive, [ADR 0025](0025-document-json-format.md)). Adding
  a second parser is overhead with no benefit. `ignore_comments=true`
  closes the only JSON ergonomics gap that mattered.
- **Singleton / global config.** Rejected: a `App`-owned snapshot is
  trivially testable and survives the eventual "headless engine + GUI
  shell" split. Globals make integration tests painful and hide the
  read sites.
- **Refuse to launch on bad config.** Rejected: a typo in a user-edited
  JSON file shouldn't lock them out of the app. The defaults still
  produce a working build, and the error is written to stderr.

## Consequences

- `app/src/app.cpp` no longer contains `kFramesInFlight`, `kStep`, or
  literal `1600 / 1000 / 16.0F / 0.1 / 32.0`. New tunables added as
  fields, not as scattered constants.
- The build output (`build/bin/`) becomes zip-distributable on Windows
  without a packaging script — `shaders/` is copied next to the `.exe`
  by the post-build step.
- The config surface is additive and forward-compatible: old config
  files survive new versions (defaults fill gaps), new configs survive
  old binaries (unknown keys ignored).
- The CJK font probe still runs — `cfg.font.cjk_font_path` only
  overrides the probe when set, so users with non-standard installs
  can fix font rendering without code changes.
- This is **Phase 1** of the AAA-grade hardcoding cleanup. Follow-ups
  (separate PRs):
  - **Phase 2:** Migrate engine performance tunables (vertex buffer
    capacity, blend mode tables) to `harness::FeatureFlag` so they're
    runtime-tweakable from the harness console.
  - **Phase 3:** Per-document tunables (page defaults, brush presets,
    custom palettes) move into `.noted` metadata so they travel with
    the document.
  - **Phase 4:** Keybinding remap config + Preferences UI hooked up to
    `harness::Config::replace_all` for hot-reload.

## References

- [ADR 0004 — Runtime harness for flexible control](0004-harness.md)
- [ADR 0025 — Document JSON serialization](0025-document-json-format.md)
- [ADR 0027 — UI stack selection](0027-ui-stack-selection.md)
