#pragma once

// Per-tool option payloads — the user-tweakable state each tool keeps.
//
// Phase B.3 of the unified-canvas plan. The tool palette flips which
// tool is active (Phase B.1); the canvas-pass split lets eraser
// strokes preserve the page pattern (Phase B.2); THIS phase gives the
// user the controls to shape each tool's behaviour (size, colour,
// pressure curve) at runtime.
//
// **Design points** (per ADR 0031 § "Per-tool option payloads"):
//
//   - One options struct per tool kind. Stored side-by-side on
//     `ToolState` rather than in a `std::variant` so the active tool's
//     payload is always trivially accessible AND the inactive tools'
//     state persists across switches (a user fiddling with the
//     eraser's size shouldn't lose their pen colour). Memory cost is
//     a handful of floats per tool — negligible.
//
//   - Pure data. Defaults match the engine's `BrushStyle{}` defaults
//     bit-for-bit so a freshly-allocated `ToolState` reproduces the
//     pre-B.3 pen behaviour exactly.
//
//   - **Hardness / softness deliberately excluded for B.3.** The
//     ribbon tessellator currently ignores `BrushStyle::softness_ratio`
//     (see `stroke_geometry.hpp`), so a hardness slider would be a
//     visible control with zero visual effect. It'll land alongside
//     the SDF-edge brush in a future phase. **No fake sliders.**
//
//   - Wire-stable layouts. When `.noted` gains per-document brush
//     presets the writer pickles these structs directly; reorder or
//     remove fields → schema bump.
//
// The conversion functions (`brush_from_pen` / `brush_from_eraser`)
// produce the `noted::stroke::BrushStyle` the engine consumes. They
// clamp pathological inputs (negative radii, NaN gamma) so a runaway
// slider can't poison the stroke buffer. The clamps are deliberate
// duplicates of `stamp_from_pressure`'s own defensive math — better
// to enforce sanity at the data boundary AND at the eval site than
// trust callers.

#include "noted/engine/stroke/stroke_geometry.hpp"

namespace noted::domain::tool {

struct BrushPreset;  // brush_preset.hpp — forward-decl avoids the
                     // options header pulling in the full preset
                     // definition for callers that just want the
                     // engine-facing conversion functions.

// Pen — freehand vector ink. Tunable parameters mirror the engine's
// `BrushStyle` 1:1 (the conversion is essentially a copy), with the
// addition that `BrushStyle::softness_ratio` is held at its default
// here too even though the UI doesn't expose it yet — preserving the
// field keeps `.noted` schemas forward-compatible.
struct PenOptions {
    // Min / max ribbon half-width in canvas pixels. Pressure
    // interpolates linearly between them. Defaults: 2..10 px (matches
    // BrushStyle defaults).
    float min_radius_px{2.0F};
    float max_radius_px{10.0F};

    // Pressure → alpha shape. The `alpha_gamma` scalar drives the
    // simple slider in the UI; whenever the slider moves the
    // `pressure_curve` below is rebuilt via
    // `PressureCurve::from_gamma`. The user can ALSO drag the
    // curve editor's handles directly, which writes the curve
    // without touching `alpha_gamma` — both knobs end up at the
    // same destination so `brush_from_pen` only forwards the curve.
    float alpha_gamma{1.8F};
    noted::stroke::PressureCurve pressure_curve{noted::stroke::PressureCurve::from_gamma(1.8F)};

    // Straight-alpha colour. Alpha is per-stroke; the pressure curve
    // multiplies it further per-sample (see `stamp_from_pressure`).
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};

    // Input-stabilizer weight (Procreate "Streamline" equivalent).
    // 0 = raw input; 0.5 = moderate jitter rejection with mild lag;
    // higher = visibly trails the cursor. Mirrored into
    // `BrushStyle::stabilizer` by `brush_from_pen`.
    float stabilizer{0.5F};

    // Soft edge as a fraction of the ribbon radius. 0 = crisp
    // capsule edge (current Hard Brush feel); 1 = full feather
    // (Soft Brush / Airbrush). The polyline fragment shader widens
    // the SDF smoothstep band by this fraction. Mirrored into
    // `BrushStyle::softness_ratio` by `brush_from_pen` so the
    // engine-facing data and the UI-facing data stay in lockstep.
    float softness{0.20F};

    // Velocity-aware size taper. 0 = pressure-only sizing; 1 =
    // strong speed taper (fast strokes shrink to 30 % of their
    // pressure-mapped radius). The engine reads
    // `BrushStyle::velocity_blend`; this PenOptions field mirrors
    // into it via `brush_from_pen`.
    float velocity_blend{0.0F};

    // Tilt-aware calligraphy width. 0 = ignore tilt; 1 = full
    // chisel effect — segments parallel to pen tilt thin to ~40 %.
    // Mouse / no-tilt devices report zero tilt so the effect
    // vanishes naturally regardless of this slider's value.
    float tilt_blend{0.0F};

    [[nodiscard]] auto operator==(const PenOptions&) const noexcept -> bool = default;
};

// Eraser — destination-out stroke. The source colour is **ignored**
// by the eraser pipeline's blend equation, so we don't expose a
// colour field. Pressure → alpha still applies (light touch partial
// erase; full pressure full erase).
struct EraserOptions {
    // Eraser footprint, in canvas pixels. Defaults are intentionally
    // larger than the pen so the user gets a sensibly-sized eraser
    // out of the box; tweakable from the UI.
    float min_radius_px{4.0F};
    float max_radius_px{20.0F};

    // Same gamma role as the pen — controls how pressure maps to
    // erase strength. Default 1.0 (linear) since "soft erase via
    // gamma curve" is less idiomatic than "soft erase via pressure
    // sensor" — easier for the user to reason about.
    float alpha_gamma{1.0F};

    [[nodiscard]] auto operator==(const EraserOptions&) const noexcept -> bool = default;
};

// PenOptions → BrushStyle. Clamps negative / NaN extents to a 1 px
// floor and ensures min ≤ max. Color components are passed through
// without clamping — ImGui's `ColorEdit*` already produces values in
// [0, 1], and clamping here would mask any future colour-space
// bug rather than fix it. Alpha gamma <= 0 falls back to 1.0
// (linear) to keep the call safe.
[[nodiscard]] auto brush_from_pen(const PenOptions& opt) noexcept -> noted::stroke::BrushStyle;

// EraserOptions → BrushStyle. Same clamp discipline. RGB defaults to
// black; the eraser pipeline ignores colour so the value is
// cosmetic, but a sane default makes the BrushStyle representable
// without surprises if the field is ever inspected.
[[nodiscard]] auto brush_from_eraser(const EraserOptions& opt) noexcept
    -> noted::stroke::BrushStyle;

// Overwrite `opt` with the stroke-engine-facing fields of `preset`.
// Honours `preset.use_preset_color` — when false the existing
// `opt.r/g/b/a` survive (artist's current-colour stays). Used by
// the brush picker UI when the user clicks a preset card.
void apply_preset_to(const BrushPreset& preset, PenOptions& opt) noexcept;

}  // namespace noted::domain::tool
