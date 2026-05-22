#include "input/camera_controller.hpp"

#include <cmath>

#include <imgui.h>

#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/hook/registry.hpp"

#include "config/app_config.hpp"

namespace noted::app::input {

namespace {

// True when ImGui currently wants the mouse — i.e. the cursor is
// over a panel or an active widget like a slider drag. Used by the
// camera handlers to short-circuit BEFORE applying pan / zoom so a
// scroll wheel inside the Color / Brush panel can't bleed into a
// canvas zoom. Safe to call before any ImGui::Begin in the frame —
// GetIO() reads cached flags set during the previous EndFrame.
[[nodiscard]] auto imgui_wants_mouse() noexcept -> bool {
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;  // headless build / test harness
    }
    return ImGui::GetIO().WantCaptureMouse;
}

}  // namespace

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
    // If a panel has the mouse (e.g. mid-drag on a slider), ignore
    // the middle-button press so we don't start a pan that races
    // the widget interaction.
    if (imgui_wants_mouse()) {
        return;
    }
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
    // Skip canvas zoom when the wheel is rolling inside an ImGui
    // panel — Color picker / Brush options / etc. ImGui already
    // consumed the event for their own scroll bars, so applying it
    // to the canvas as well produced the "panel scroll also moves
    // the page" bug.
    if (imgui_wants_mouse()) {
        return;
    }
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
    // Window extent tracks the framebuffer 1:1.
    //
    // Canvas extent is NO LONGER tied to the framebuffer (ADR 0033
    // Slice 2). It's driven by `App::ensure_canvas_fits_pages` to
    // contain the full page stack, which can be taller than the
    // window. Re-asserting canvas_extent here would race the host's
    // every-frame sync and misalign pointer ink against the rendered
    // ribbon, so we deliberately leave it alone.
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
