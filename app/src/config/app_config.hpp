#pragma once

// AppConfig — typed runtime configuration for the noted app.
//
// **Phase 1 of the AAA-grade hardcoding cleanup (ADR 0030).** Holds
// the user-facing tunables that previously lived as `constexpr`
// literals scattered across `app/src/app.cpp` and friends. Defaults
// match the prior hardcoded values exactly, so loading the default
// config produces an app that behaves identically to the pre-PR
// build. Adding fields is additive: old config files don't break.
//
// Source-of-truth priority (highest wins):
//   1. **Environment variables** (per-field — e.g. `NOTED_SHADER_DIR`)
//   2. **`noted.config.json` next to the .exe** (parsed via
//      nlohmann/json, unknown keys ignored)
//   3. **`AppConfig::defaults()`** — what the app shipped with
//
// What's NOT in here:
//   - Engine-internal tunables (vertex buffer capacity, blend
//     factors, push-constant layout sizes) — those live with the
//     code that owns the contract.
//   - Hard Vulkan / platform contracts (API version, descriptor
//     binding rules).
//   - Theming colours (already a separate axis — ADR 0027 +
//     `noted::ui::theme::apply`).
//
// Future phases (separate PRs):
//   - Phase 2: harness FeatureFlag migration for performance
//     tunables (`flag_force_vsync` is the existing pattern).
//   - Phase 3: per-document `.noted` metadata (page defaults,
//     brush presets, custom palettes).
//   - Phase 4: keybinding remap config + Preferences UI.

#include <cstdint>
#include <filesystem>
#include <string>

#include "noted/engine/canvas/page.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/ui/theme/theme.hpp"

namespace noted::app::config {

struct WindowConfig {
    std::uint32_t width{1600};
    std::uint32_t height{1000};
    std::string title{"noted"};
};

struct CanvasConfig {
    // Renderer frame slot count. Must fit in
    // `noted::compositor::LayerCompositor::kMaxFramesInFlight`
    // (currently 4). Increasing this trades GPU memory for
    // potentially smoother input latency under high load.
    std::uint32_t frames_in_flight{2};
    // Camera scroll-zoom step (per scroll-wheel notch). 1.0
    // disables zooming; values close to 1.0 feel fine, larger
    // values feel coarse.
    double zoom_step{1.1};
    // Camera scale clamp. Below `zoom_min` the canvas becomes a
    // dot; above `zoom_max` pixels become house-sized.
    double zoom_min{0.1};
    double zoom_max{32.0};

    // Default extent + background for newly added pages — both for
    // the initial demo seed and for the "+ Add page" button in the
    // page strip. US Letter at 72 DPI matches `Page`'s in-struct
    // defaults; surfacing them here lets a future Preferences UI
    // flip the default without recompiling.
    float default_page_extent_w_px{612.0F};
    float default_page_extent_h_px{792.0F};
    noted::canvas::PageBackground default_page_background{noted::canvas::PageBackground::grid};
};

struct FontConfig {
    // Optional override for the OS-installed CJK font probe.
    // Empty → the app falls back to the per-OS candidate list
    // (Malgun Gothic on Windows, Apple SD Gothic Neo on macOS,
    // Noto Sans CJK on Linux).
    std::filesystem::path cjk_font_path{};
    // Pixel size for the loaded font. 16 is the smallest that
    // keeps Hangul legible without subpixel hinting.
    float size_px{16.0F};
};

struct AssetConfig {
    // Optional override for the SPV shader directory. When set
    // and the directory exists, the app uses it verbatim. When
    // empty, the resolver walks:
    //   1. `NOTED_SHADER_DIR` env var
    //   2. `<exe_dir>/shaders` (production layout)
    //   3. The compile-time `NOTED_SHADER_DIR` define (dev
    //      fallback so the source-tree workflow still works)
    std::filesystem::path shader_dir{};
};

struct UiConfig {
    noted::ui::theme::ThemeKind default_theme{noted::ui::theme::ThemeKind::dark};
    // Whether the page strip side rail is visible on startup. The
    // user can still toggle it via View → Page strip; this just
    // picks the initial state.
    bool show_page_strip{true};
};

struct AppConfig {
    WindowConfig window{};
    CanvasConfig canvas{};
    FontConfig font{};
    AssetConfig assets{};
    UiConfig ui{};

    // The shipping defaults — equivalent to the pre-PR
    // hardcoded values. Calling code that doesn't load a file
    // should use this so behaviour is identical to v0.x prior.
    [[nodiscard]] static auto defaults() noexcept -> AppConfig { return AppConfig{}; }

    // Parse a JSON config file. Schema = same as the struct
    // (camelCase keys: `window.width`, `canvas.framesInFlight`,
    // ...). Unknown keys are ignored — old configs survive new
    // versions, new configs survive old binaries (forward-
    // compatible). Missing keys fall back to `defaults()`.
    [[nodiscard]] static auto load_from_file(const std::filesystem::path& path)
        -> Result<AppConfig>;
};

// Resolve the final shader directory by walking the AssetConfig
// override → env var → `<exe_dir>/shaders` → compile-time fallback
// search list. Returns the first directory that actually exists
// (a missing dir at the head of the list is silently skipped). An
// empty return means every candidate failed — the caller should
// surface this as a fatal error.
[[nodiscard]] auto resolve_shader_dir(const AppConfig& cfg) -> std::filesystem::path;

// Load the app config from the standard search path:
//   1. `<exe_dir>/noted.config.json`
// Returns `AppConfig::defaults()` when no config file is present
// or when parsing fails (parse errors are written to stderr but
// don't propagate — a malformed config shouldn't refuse to launch).
[[nodiscard]] auto load_default() -> AppConfig;

}  // namespace noted::app::config
