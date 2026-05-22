#pragma once

// BrushPreset — a stable, named description of how a stroke should
// look and behave. Composed of every parameter the stroke engine
// reads at press time (size range, pressure curve, color, smoothing)
// PLUS the extended-dynamics parameters needed to characterise
// brush "feel" the way Photoshop / Procreate / Clip Studio do
// (spacing, scatter, edge softness, stamp angle / jitter).
//
// Presets are pure data: no GPU, no I/O. A library owns the
// collection (see `BrushLibrary`); the UI surfaces them as a picker;
// the App glues "user clicked a preset" → "PenOptions overwritten
// from preset" so the next stroke uses the new style.
//
// Wire-stable: `id` is monotonically allocated by the owning
// library and persisted on disk (sidecar `brushes.json`); `kind`
// ordinals never reorder so a v1 preset bundle keeps loading in v2.

#include <cstdint>
#include <string>

#include "noted/engine/stroke/stroke_geometry.hpp"

namespace noted::domain::tool {

// Coarse brush category — drives the picker grid's grouping and the
// default-icon glyph. Wire-stable ordinals: new kinds append, never
// reorder.
enum class BrushKind : std::uint8_t {
    pen = 0,          // hard-edged ink, full opacity
    pencil = 1,       // graphite — softer, lower opacity, pressure-driven
    marker = 2,       // flat-tip, semi-transparent overlap multiplies
    soft_brush = 3,   // airy / feathered edge
    hard_brush = 4,   // crisp edge, paint-like
    airbrush = 5,     // very soft spray, builds with strokes
    calligraphy = 6,  // angled / oval stamp
    texture = 7,      // future: textured imprint
};

// Stable preset identifier. `0` is reserved for "no preset" /
// invalid; built-in presets carry the same id space as user-added
// ones, allocated monotonically by the owning `BrushLibrary`.
using BrushPresetId = std::uint64_t;
inline constexpr BrushPresetId invalid_brush_preset_id = 0;

struct BrushPreset {
    BrushPresetId id{invalid_brush_preset_id};
    std::string name;
    BrushKind kind{BrushKind::pen};

    // ---- Stroke-engine parameters (mirror PenOptions) ----------------
    float min_radius_px{2.0F};
    float max_radius_px{10.0F};
    // Pressure → alpha gamma scalar (drives the UI's simple
    // slider). The authoritative shaping curve below is rebuilt
    // from this via `PressureCurve::from_gamma` when the preset
    // ships with the default curve, OR a hand-tuned curve overrides
    // the gamma derivation for a more bespoke pen feel.
    float alpha_gamma{1.8F};
    noted::stroke::PressureCurve pressure_curve{noted::stroke::PressureCurve::from_gamma(1.8F)};
    // RGBA. Applied to every stamp's color when `use_preset_color`
    // is true; otherwise the active Color panel's value wins.
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};
    // Procreate "Streamline" equivalent. 0 = raw input, 0.5 =
    // moderate smoothing, 0.9 = visible lag.
    float stabilizer{0.5F};

    // ---- Extended dynamics (Phase 1: data only, future shader work) -
    // Edge softness as a fraction of radius. 0 = crisp edge, 1 = fully
    // feathered. Reserved for the upcoming SDF-edge brush; the v1
    // ribbon tessellator ignores it but round-trips the value.
    float softness{0.20F};
    // Stamp spacing as a fraction of radius. 0.10 = dense ribbon
    // (looks continuous), 1.0 = stamps every-radius apart (visible
    // dots), 2.0+ = sparse pattern. The vector-ink path always
    // looks continuous because it's a polyline, not stamp-based;
    // this value is plumbed for the future stamp brushes.
    float spacing{0.10F};
    // Positional scatter (radius units). 0 = stamps land on the
    // centerline, 1 = stamps jitter up to ±1 radius perpendicular.
    float scatter{0.0F};
    // Base stamp angle in degrees (0-180; angle is symmetric).
    float angle_deg{0.0F};
    // Random angle delta per stamp (0-1; 1 = ±90°).
    float angle_jitter{0.0F};

    // When true, the preset overrides the panel's live color on
    // apply. When false, the panel color is preserved — useful for
    // presets that define "shape feel" but should track whatever
    // colour the user is currently mixing.
    bool use_preset_color{true};

    [[nodiscard]] auto operator==(const BrushPreset&) const noexcept -> bool = default;
};

// Human-readable label for a `BrushKind`. Used by the picker grid +
// the "Save as preset" dialog. ASCII-only so it doesn't depend on
// the optional CJK / symbol fonts.
[[nodiscard]] auto label_of(BrushKind kind) noexcept -> const char*;

}  // namespace noted::domain::tool
