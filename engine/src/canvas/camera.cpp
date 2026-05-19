#include "noted/engine/canvas/camera.hpp"

#include <cmath>

namespace noted::canvas {

namespace {

// Internal scale floor / ceiling for `zoom_around` so a runaway
// scroll event can't pin the camera at degenerate values. Public
// `clamp_scale` lets the caller pick its own range; this internal
// guard exists only to make `zoom_around` self-consistent against
// NaN / inf / zero `factor` inputs.
constexpr double kInternalScaleFloor = 1e-6;
constexpr double kInternalScaleCeiling = 1e+6;

[[nodiscard]] auto sanitized_factor(double factor) noexcept -> double {
    // NaN check via self-comparison — fast and avoids <cmath>'s
    // std::isnan which on MSVC needs /fp:precise (we use the project
    // default, which is fine, but the self-compare is portable).
    if (factor != factor) {
        return 1.0;
    }
    if (factor <= 0.0) {
        return 1.0;
    }
    if (std::isinf(factor)) {
        return 1.0;
    }
    return factor;
}

[[nodiscard]] auto safe_extent(std::uint32_t v) noexcept -> double {
    // Treat zero as "1 pixel" so the divisions downstream don't
    // explode during the brief windows when the surface is
    // minimized or before the swapchain has a real extent.
    return v == 0U ? 1.0 : static_cast<double>(v);
}

}  // namespace

void Camera::set_translation(double x, double y) noexcept {
    translation_x_ = x;
    translation_y_ = y;
}

void Camera::set_scale(double s) noexcept {
    if (s != s || s <= 0.0 || std::isinf(s)) {
        return;
    }
    scale_ = s;
}

void Camera::translate_by(double dx, double dy) noexcept {
    translation_x_ += dx;
    translation_y_ += dy;
}

void Camera::set_canvas_extent(std::uint32_t w, std::uint32_t h) noexcept {
    canvas_w_ = safe_extent(w);
    canvas_h_ = safe_extent(h);
}

void Camera::set_window_extent(std::uint32_t w, std::uint32_t h) noexcept {
    window_w_ = safe_extent(w);
    window_h_ = safe_extent(h);
}

void Camera::clamp_scale(double min_scale, double max_scale) noexcept {
    // Defensive: caller might pass crossed bounds. Pick the actual
    // floor/ceiling rather than assuming order.
    const double lo = (min_scale < max_scale) ? min_scale : max_scale;
    const double hi = (min_scale < max_scale) ? max_scale : min_scale;
    if (scale_ < lo) {
        scale_ = lo;
    } else if (scale_ > hi) {
        scale_ = hi;
    }
}

void Camera::zoom_around(double anchor_screen_x, double anchor_screen_y, double factor) noexcept {
    const double f = sanitized_factor(factor);

    // Pin invariant: after the operation, unproject(anchor) must
    // equal what it equalled before. Derive new translation from
    // the closed-form: anchor = T_old + cx * scale_old, target
    // anchor = T_new + cx * scale_new, so T_new = anchor - cx *
    // scale_new.
    const double cx = unproject_x(anchor_screen_x);
    const double cy = unproject_y(anchor_screen_y);

    double new_scale = scale_ * f;
    if (new_scale < kInternalScaleFloor) {
        new_scale = kInternalScaleFloor;
    } else if (new_scale > kInternalScaleCeiling) {
        new_scale = kInternalScaleCeiling;
    }
    scale_ = new_scale;
    translation_x_ = anchor_screen_x - cx * scale_;
    translation_y_ = anchor_screen_y - cy * scale_;
}

auto Camera::shader_scale_x() const noexcept -> float {
    // 2 * scale * canvas_w / window_w. With scale=1 and
    // canvas==window, this is exactly 2 — the original
    // fullscreen-triangle factor.
    return static_cast<float>(2.0 * scale_ * canvas_w_ / window_w_);
}

auto Camera::shader_scale_y() const noexcept -> float {
    return static_cast<float>(2.0 * scale_ * canvas_h_ / window_h_);
}

auto Camera::shader_translation_x() const noexcept -> float {
    // 2 * translation / window - 1. With translation=0 this gives
    // -1 — again the original baseline.
    return static_cast<float>(2.0 * translation_x_ / window_w_ - 1.0);
}

auto Camera::shader_translation_y() const noexcept -> float {
    return static_cast<float>(2.0 * translation_y_ / window_h_ - 1.0);
}

}  // namespace noted::canvas
