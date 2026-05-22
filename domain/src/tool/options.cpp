#include "noted/domain/tool/options.hpp"

#include <algorithm>
#include <cmath>

#include "noted/domain/tool/brush_preset.hpp"

namespace noted::domain::tool {

namespace {

[[nodiscard]] auto safe_radius(float v) noexcept -> float {
    // Mirror `Page::safe_extent`'s posture: negative or NaN collapses
    // to a 1 px floor so a runaway slider can't divide-by-zero the
    // tessellator or push a degenerate ribbon into the vertex buffer.
    if (std::isnan(v) || v < 1.0F) {
        return 1.0F;
    }
    return v;
}

[[nodiscard]] auto safe_gamma(float v) noexcept -> float {
    // `stamp_from_pressure` already does this defensively; mirroring
    // it here means a malformed payload can never poison BrushStyle
    // downstream.
    if (std::isnan(v) || v <= 0.0F) {
        return 1.0F;
    }
    return v;
}

[[nodiscard]] auto ordered_radii(float lo, float hi) noexcept -> std::pair<float, float> {
    // Allow the user to drag min ABOVE max temporarily — re-order on
    // the way out so the pressure interpolation isn't inverted (which
    // would shrink the brush with increasing pressure, the opposite
    // of what every other paint app does).
    const auto a = safe_radius(lo);
    const auto b = safe_radius(hi);
    return {std::min(a, b), std::max(a, b)};
}

}  // namespace

auto brush_from_pen(const PenOptions& opt) noexcept -> noted::stroke::BrushStyle {
    const auto [lo, hi] = ordered_radii(opt.min_radius_px, opt.max_radius_px);
    noted::stroke::BrushStyle b{};
    b.min_radius_px = lo;
    b.max_radius_px = hi;
    // Soft-edge fraction now plumbed end-to-end: presets dial it,
    // brush_options exposes a slider, the polyline fragment shader
    // widens its SDF smoothstep band by this fraction. Clamp at the
    // boundary in case a degenerate value sneaks in.
    b.softness_ratio = std::clamp(opt.softness, 0.0F, 1.0F);
    b.alpha_gamma = safe_gamma(opt.alpha_gamma);
    // Curve is authoritative for the engine's stamp shaping.
    b.pressure_curve = opt.pressure_curve;
    b.velocity_blend = std::clamp(opt.velocity_blend, 0.0F, 1.0F);
    b.r = opt.r;
    b.g = opt.g;
    b.b = opt.b;
    b.a = opt.a;
    // Clamp stabilizer at the data boundary so a runaway slider
    // value can't lock the smoothed cursor in place.
    b.stabilizer = (opt.stabilizer < 0.0F)    ? 0.0F
                   : (opt.stabilizer > 0.95F) ? 0.95F
                                              : opt.stabilizer;
    return b;
}

void apply_preset_to(const BrushPreset& preset, PenOptions& opt) noexcept {
    // Stroke-shape fields always overwrite — the preset's whole point
    // is to define how the brush feels, so the user's previous
    // min/max/pressure/stabilizer get replaced.
    opt.min_radius_px = preset.min_radius_px;
    opt.max_radius_px = preset.max_radius_px;
    opt.alpha_gamma = preset.alpha_gamma;
    opt.pressure_curve = preset.pressure_curve;
    opt.stabilizer = preset.stabilizer;
    opt.softness = preset.softness;
    opt.velocity_blend = preset.velocity_blend;
    if (preset.use_preset_color) {
        // Preset wants its colour applied — e.g. "Soft Pencil" ships
        // graphite-grey, "Calligraphy" ships pure black.
        opt.r = preset.r;
        opt.g = preset.g;
        opt.b = preset.b;
        opt.a = preset.a;
    } else {
        // Preset's `r/g/b` are ignored; the artist's current Colour
        // picker selection survives. Alpha is still taken from the
        // preset (different brushes deposit different opacity even
        // with the same hue — Marker is partial, Ink Pen is solid).
        opt.a = preset.a;
    }
}

auto brush_from_eraser(const EraserOptions& opt) noexcept -> noted::stroke::BrushStyle {
    const auto [lo, hi] = ordered_radii(opt.min_radius_px, opt.max_radius_px);
    noted::stroke::BrushStyle b{};
    b.min_radius_px = lo;
    b.max_radius_px = hi;
    b.softness_ratio = 0.20F;
    b.alpha_gamma = safe_gamma(opt.alpha_gamma);
    // RGB cosmetic only — the destination-out blend ignores src
    // colour. Black is the safest default; if the eraser is ever
    // dumped for debugging the RGBA reads as "transparent black at
    // 100% alpha" which is the intuitive eraser tile.
    b.r = 0.0F;
    b.g = 0.0F;
    b.b = 0.0F;
    b.a = 1.0F;
    return b;
}

}  // namespace noted::domain::tool
