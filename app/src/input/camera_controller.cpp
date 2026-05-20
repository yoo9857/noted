#include "input/camera_controller.hpp"

#include <cmath>

#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/hook/registry.hpp"

#include "config/app_config.hpp"

namespace noted::app::input {

CameraController::CameraController(noted::canvas::Camera& camera,
                                   const noted::app::config::CanvasConfig& cfg) noexcept
    : camera_(camera), cfg_(cfg) {}

CameraController::CameraController(TestingTag,
                                   noted::canvas::Camera& camera,
                                   const noted::app::config::CanvasConfig& cfg) noexcept
    : camera_(camera), cfg_(cfg) {}

auto CameraController::create(noted::hook::Registry& registry,
                              noted::canvas::Camera& camera,
                              const noted::app::config::CanvasConfig& cfg)
    -> std::unique_ptr<CameraController> {
    // Heap-allocate before subscribing — the lambdas capture `this`,
    // which must stay stable. Matches the StrokeEngine / ToolInputRouter
    // pattern.
    auto self = std::unique_ptr<CameraController>(new CameraController{camera, cfg});
    auto* p = self.get();

    p->sub_pressed_ = noted::hook::Subscription<noted::hook::PointerPressed>{
        registry.on_pointer_pressed,
        registry.on_pointer_pressed.subscribe([p](const noted::hook::PointerPressed& e) {
            if (e.button == noted::hook::PointerButton::middle) {
                p->on_pressed_middle(e.x, e.y);
            }
        })};
    p->sub_released_ = noted::hook::Subscription<noted::hook::PointerReleased>{
        registry.on_pointer_released,
        registry.on_pointer_released.subscribe([p](const noted::hook::PointerReleased& e) {
            if (e.button == noted::hook::PointerButton::middle) {
                p->on_released_middle();
            }
        })};
    p->sub_moved_ = noted::hook::Subscription<noted::hook::PointerMoved>{
        registry.on_pointer_moved,
        registry.on_pointer_moved.subscribe(
            [p](const noted::hook::PointerMoved& e) { p->on_moved(e.x, e.y); })};
    p->sub_scrolled_ = noted::hook::Subscription<noted::hook::Scrolled>{
        registry.on_scrolled, registry.on_scrolled.subscribe([p](const noted::hook::Scrolled& e) {
            p->on_scrolled(e.dy);
        })};
    p->sub_resized_ = noted::hook::Subscription<noted::hook::FramebufferResized>{
        registry.on_framebuffer_resized,
        registry.on_framebuffer_resized.subscribe([p](const noted::hook::FramebufferResized& r) {
            p->on_framebuffer_resized(r.width, r.height);
        })};
    return self;
}

void CameraController::on_pressed_middle(double x, double y) noexcept {
    panning_ = true;
    pan_last_x_ = x;
    pan_last_y_ = y;
}

void CameraController::on_released_middle() noexcept {
    panning_ = false;
}

void CameraController::on_moved(double x, double y) noexcept {
    if (panning_) {
        const double dx = x - pan_last_x_;
        const double dy = y - pan_last_y_;
        camera_.translate_by(dx, dy);
    }
    cursor_x_ = x;
    cursor_y_ = y;
    pan_last_x_ = x;
    pan_last_y_ = y;
}

void CameraController::on_scrolled(double dy) noexcept {
    // `cfg_` is a live reference (not a snapshot) — a future
    // Preferences UI can flip zoom_step / zoom_min / zoom_max
    // without restart and the next scroll event picks up the new
    // values. Camera's internal sanitize_factor clamps a runaway
    // NaN / inf / 0 dy so a transient bad input cannot lock the
    // camera into a degenerate state.
    const double factor = std::pow(cfg_.zoom_step, dy);
    camera_.zoom_around(cursor_x_, cursor_y_, factor);
    camera_.clamp_scale(cfg_.zoom_min, cfg_.zoom_max);
}

void CameraController::on_framebuffer_resized(unsigned w, unsigned h) noexcept {
    // The canvas extent matches the window extent today; if a future
    // PR splits them (e.g. doc-resolution canvas with windowed
    // downsample) this assignment splits.
    camera_.set_canvas_extent(w, h);
    camera_.set_window_extent(w, h);
}

void CameraController::inject_press_middle_(double x, double y) {
    on_pressed_middle(x, y);
}
void CameraController::inject_release_middle_(double /*x*/, double /*y*/) {
    on_released_middle();
}
void CameraController::inject_move_(double x, double y) {
    on_moved(x, y);
}
void CameraController::inject_scroll_(double dy) {
    on_scrolled(dy);
}
void CameraController::inject_framebuffer_resize_(unsigned w, unsigned h) {
    on_framebuffer_resized(w, h);
}

}  // namespace noted::app::input
