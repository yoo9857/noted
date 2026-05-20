#pragma once

// CameraController — input plumbing for the canvas camera.
//
// Phase R.2 of the App-layer decomposition (per ADR 0032). Owns
// every pointer / scroll / framebuffer event that mutates or reads
// the camera state, plus the in-flight pan + cursor-tracker state
// the scroll-to-zoom-around-cursor handler needs.
//
// What the controller does:
//   - Middle-button press / release toggles pan mode.
//   - Pointer-moved inside pan mode translates the camera by the
//     drag delta. Outside pan mode, it just updates the cursor
//     cache so the scroll handler can anchor zoom there.
//   - Scroll-wheel zooms around the cached cursor position with
//     multiplicative `zoom_step ^ dy`. Result is clamped to
//     `[zoom_min, zoom_max]` from the config (re-read every event
//     so a future Preferences UI can flip them live without
//     restart).
//   - Framebuffer-resize updates the camera's canvas + window
//     extent so the shader-space transform stays in sync. The
//     swapchain image resize itself happens through App's
//     `recreate_swapchain` path (different concern).
//
// Lifetime: non-movable + heap-allocated. The hook subscriptions
// capture `this`; moving would dangle the pointer. App owns it as
// `std::unique_ptr<CameraController>`.

#include <memory>

#include "noted/engine/hook/hook.hpp"

namespace noted::canvas {
class Camera;
}  // namespace noted::canvas

namespace noted::hook {
class Registry;
}  // namespace noted::hook

namespace noted::app::config {
struct CanvasConfig;
}  // namespace noted::app::config

namespace noted::app::input {

class CameraController {
public:
    // Subscribes to PointerMoved / Pressed / Released / Scrolled /
    // FramebufferResized. The `camera` reference is mutated; the
    // `cfg` reference is read for zoom_step / zoom_min / zoom_max
    // and re-read each event for live-tunability.
    [[nodiscard]] static auto create(noted::hook::Registry& registry,
                                     noted::canvas::Camera& camera,
                                     const noted::app::config::CanvasConfig& cfg)
        -> std::unique_ptr<CameraController>;

    CameraController(const CameraController&) = delete;
    auto operator=(const CameraController&) -> CameraController& = delete;
    CameraController(CameraController&&) = delete;
    auto operator=(CameraController&&) -> CameraController& = delete;
    ~CameraController() = default;

    // Inspector — used by tests + future debug overlay. Returns the
    // most recent pointer screen coords. Stays at (0, 0) before any
    // pointer event has been seen.
    [[nodiscard]] auto cursor_x() const noexcept -> double { return cursor_x_; }
    [[nodiscard]] auto cursor_y() const noexcept -> double { return cursor_y_; }
    [[nodiscard]] auto is_panning() const noexcept -> bool { return panning_; }

    // Test-only injection helpers — bypass the hook system so unit
    // tests can drive the controller without a Registry. Mirror what
    // the GLFW callbacks do.
    struct TestingTag {};
    explicit CameraController(TestingTag,
                              noted::canvas::Camera& camera,
                              const noted::app::config::CanvasConfig& cfg) noexcept;
    void inject_press_middle_(double x, double y);
    void inject_release_middle_(double x, double y);
    void inject_move_(double x, double y);
    void inject_scroll_(double dy);
    void inject_framebuffer_resize_(unsigned w, unsigned h);

private:
    CameraController(noted::canvas::Camera& camera,
                     const noted::app::config::CanvasConfig& cfg) noexcept;

    void on_pressed_middle(double x, double y) noexcept;
    void on_released_middle() noexcept;
    void on_moved(double x, double y) noexcept;
    void on_scrolled(double dy) noexcept;
    void on_framebuffer_resized(unsigned w, unsigned h) noexcept;

    noted::canvas::Camera& camera_;
    const noted::app::config::CanvasConfig& cfg_;

    // Cursor cache — read by the scroll handler so wheel zoom
    // anchors under the pointer (Goodnotes / Procreate behaviour).
    double cursor_x_{0.0};
    double cursor_y_{0.0};

    // Middle-button drag state.
    bool panning_{false};
    double pan_last_x_{0.0};
    double pan_last_y_{0.0};

    // RAII subscriptions — released on destruction.
    noted::hook::Subscription<noted::hook::PointerPressed> sub_pressed_{};
    noted::hook::Subscription<noted::hook::PointerReleased> sub_released_{};
    noted::hook::Subscription<noted::hook::PointerMoved> sub_moved_{};
    noted::hook::Subscription<noted::hook::Scrolled> sub_scrolled_{};
    noted::hook::Subscription<noted::hook::FramebufferResized> sub_resized_{};
};

}  // namespace noted::app::input
