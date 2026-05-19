#pragma once

// Camera — pan + uniform-scale transform between three coordinate spaces:
//
//   1. canvas   — pixels of the offscreen `CanvasRenderTarget`. The
//                 ink, layer composite, and selection mask all live in
//                 these units. (0, 0) is top-left.
//   2. screen   — pixels of the GLFW window / swapchain image. GLFW
//                 pointer events arrive in this space. (0, 0) is
//                 top-left.
//   3. NDC      — Vulkan normalized device coordinates [-1, 1] for the
//                 shader's gl_Position output. Y points down.
//
// The transform is `screen = translation + canvas * scale`. Inverting:
// `canvas = (screen - translation) / scale`. Uniform scale only — a
// non-uniform axis-aligned scale would only matter for anamorphic
// page support which isn't on the roadmap.
//
// Two consumer surfaces:
//   - `unproject_*` for the input path (screen pointer event →
//     canvas-pixel stroke).
//   - `shader_scale_*` / `shader_translation_*` for the composite
//     pass's fullscreen-triangle vertex shader, which needs scale +
//     translation in NDC units derived from the same camera. See
//     `shaders/fullscreen.slang` for the matching push constant.
//
// `zoom_around(anchor)` is the standard "scroll to zoom under the
// cursor" operation: after the call, the canvas pixel that was under
// `anchor` before the zoom is still under it. Math is in the .cpp.
//
// Pure data + pure logic — no GLFW, no Vulkan, no graphics deps.
// Lives in `engine/canvas/` so the unit-test layer can verify the
// project/unproject + zoom invariants without spinning up a GPU.

#include <cstdint>

namespace noted::canvas {

class Camera {
public:
    Camera() = default;

    // ---- Setters --------------------------------------------------------

    void set_translation(double x, double y) noexcept;
    void set_scale(double s) noexcept;
    void translate_by(double dx, double dy) noexcept;

    // Update the canvas / window extents. The shader-space helpers
    // depend on both, so callers must keep these in sync with the
    // actual `CanvasRenderTarget` extent and the swapchain image
    // extent. Both default to 1×1 which is harmless but produces a
    // useless transform until set.
    void set_canvas_extent(std::uint32_t w, std::uint32_t h) noexcept;
    void set_window_extent(std::uint32_t w, std::uint32_t h) noexcept;

    // Clamp `scale` into a sane range — guards against the user
    // zooming out to 1e-10 (canvas becomes a dot, hard to recover)
    // or in to 1e10 (numerical blowup). Idempotent.
    void clamp_scale(double min_scale, double max_scale) noexcept;

    // Multiply scale by `factor` while keeping the canvas point that
    // sat under `anchor_screen` pinned to that screen position. The
    // standard "scroll-to-zoom under the cursor" feel.
    //
    // Clamps `factor` to a sane non-zero positive range so a bad
    // input (e.g. NaN from a phantom scroll event) cannot lock the
    // camera into an unrecoverable state.
    void zoom_around(double anchor_screen_x, double anchor_screen_y, double factor) noexcept;

    // ---- Getters --------------------------------------------------------

    [[nodiscard]] auto translation_x() const noexcept -> double { return translation_x_; }
    [[nodiscard]] auto translation_y() const noexcept -> double { return translation_y_; }
    [[nodiscard]] auto scale() const noexcept -> double { return scale_; }
    [[nodiscard]] auto canvas_extent_w() const noexcept -> double { return canvas_w_; }
    [[nodiscard]] auto canvas_extent_h() const noexcept -> double { return canvas_h_; }
    [[nodiscard]] auto window_extent_w() const noexcept -> double { return window_w_; }
    [[nodiscard]] auto window_extent_h() const noexcept -> double { return window_h_; }

    // ---- Projections ----------------------------------------------------

    [[nodiscard]] auto project_x(double canvas_x) const noexcept -> double {
        return translation_x_ + canvas_x * scale_;
    }
    [[nodiscard]] auto project_y(double canvas_y) const noexcept -> double {
        return translation_y_ + canvas_y * scale_;
    }
    [[nodiscard]] auto unproject_x(double screen_x) const noexcept -> double {
        return (screen_x - translation_x_) / scale_;
    }
    [[nodiscard]] auto unproject_y(double screen_y) const noexcept -> double {
        return (screen_y - translation_y_) / scale_;
    }

    // ---- Shader push-constant helpers -----------------------------------
    //
    // The composite pass's vertex shader applies `ndc = uv * scale +
    // translation`. With default uv ∈ [0, 1] (fullscreen-triangle
    // parameterization) and an identity camera, that needs scale =
    // (2, 2) and translation = (-1, -1) to recover the standard
    // [0,1] → [-1,1] map. The helpers below compute the values
    // including the camera contribution, in floats to match the
    // shader's float2 push field.

    [[nodiscard]] auto shader_scale_x() const noexcept -> float;
    [[nodiscard]] auto shader_scale_y() const noexcept -> float;
    [[nodiscard]] auto shader_translation_x() const noexcept -> float;
    [[nodiscard]] auto shader_translation_y() const noexcept -> float;

private:
    double translation_x_{0.0};
    double translation_y_{0.0};
    double scale_{1.0};
    double canvas_w_{1.0};
    double canvas_h_{1.0};
    double window_w_{1.0};
    double window_h_{1.0};
};

}  // namespace noted::canvas
