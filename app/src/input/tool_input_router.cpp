#include "input/tool_input_router.hpp"

#include <utility>

#include <imgui.h>

#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::app::input {

namespace {

[[nodiscard]] auto find_handler(std::vector<std::unique_ptr<ToolInputHandler>>& handlers,
                                noted::domain::tool::ToolKind kind) -> ToolInputHandler* {
    for (auto& h : handlers) {
        if (h && h->handled_kind() == kind) {
            return h.get();
        }
    }
    return nullptr;
}

}  // namespace

auto ToolInputRouter::create(noted::hook::Registry& registry, const noted::canvas::Camera& camera)
    -> std::unique_ptr<ToolInputRouter> {
    // Heap-allocate before subscribing so the `this`-capturing lambdas
    // see a stable address. unique_ptr is the standard expression of
    // that contract — see StrokeEngine for the same pattern.
    auto router = std::unique_ptr<ToolInputRouter>(new ToolInputRouter{});
    router->camera_ = &camera;

    auto* self = router.get();
    router->sub_pressed_ = noted::hook::Subscription<noted::hook::PointerPressed>{
        registry.on_pointer_pressed,
        registry.on_pointer_pressed.subscribe([self](const noted::hook::PointerPressed& e) {
            if (e.button != noted::hook::PointerButton::left) {
                return;
            }
            // Modifier snapshot at PRESS time (matches Photoshop /
            // Figma: releasing Shift mid-drag keeps the union the
            // user meant when they started). ImGui's IO is updated
            // by the GLFW backend on every key event, so by the time
            // a pointer event fires the modifier state is current.
            const auto& io = ImGui::GetIO();
            self->on_pressed(e.x, e.y, io.KeyShift, io.KeyAlt);
        })};
    router->sub_moved_ = noted::hook::Subscription<noted::hook::PointerMoved>{
        registry.on_pointer_moved,
        registry.on_pointer_moved.subscribe(
            [self](const noted::hook::PointerMoved& e) { self->on_moved(e.x, e.y); })};
    router->sub_released_ = noted::hook::Subscription<noted::hook::PointerReleased>{
        registry.on_pointer_released,
        registry.on_pointer_released.subscribe([self](const noted::hook::PointerReleased& e) {
            if (e.button != noted::hook::PointerButton::left) {
                return;
            }
            self->on_released(e.x, e.y);
        })};
    return router;
}

void ToolInputRouter::register_handler(std::unique_ptr<ToolInputHandler> handler) {
    if (!handler) {
        return;
    }
    // First-registered-wins for duplicates. v0.x doesn't have a use
    // case for replacing a handler, and the silent ignore makes
    // duplicate registrations a no-op rather than a footgun. A future
    // `validate()` could surface this if it becomes a real issue.
    if (find_handler(handlers_, handler->handled_kind()) != nullptr) {
        return;
    }
    handlers_.push_back(std::move(handler));
}

void ToolInputRouter::set_active(noted::domain::tool::ToolKind kind) {
    auto* next = find_handler(handlers_, kind);
    if (next == active_) {
        // No-op switch (re-applying the current kind). Don't fire
        // on_deactivated → on_activate round-trip; handlers expect
        // those calls to bracket a real transition.
        return;
    }
    if (active_ != nullptr) {
        active_->on_deactivated();
    }
    active_ = next;
}

void ToolInputRouter::on_pressed(double sx, double sy, bool shift, bool alt) {
    if (active_ == nullptr) {
        return;
    }
    const double cx = (camera_ != nullptr) ? camera_->unproject_x(sx) : sx;
    const double cy = (camera_ != nullptr) ? camera_->unproject_y(sy) : sy;
    active_->on_pressed(cx, cy, shift, alt);
}

void ToolInputRouter::on_moved(double sx, double sy) {
    if (active_ == nullptr) {
        return;
    }
    const double cx = (camera_ != nullptr) ? camera_->unproject_x(sx) : sx;
    const double cy = (camera_ != nullptr) ? camera_->unproject_y(sy) : sy;
    active_->on_moved(cx, cy);
}

void ToolInputRouter::on_released(double sx, double sy) {
    if (active_ == nullptr) {
        return;
    }
    const double cx = (camera_ != nullptr) ? camera_->unproject_x(sx) : sx;
    const double cy = (camera_ != nullptr) ? camera_->unproject_y(sy) : sy;
    active_->on_released(cx, cy);
}

void ToolInputRouter::inject_press_(double sx, double sy, bool shift, bool alt) {
    on_pressed(sx, sy, shift, alt);
}
void ToolInputRouter::inject_move_(double sx, double sy) {
    on_moved(sx, sy);
}
void ToolInputRouter::inject_release_(double sx, double sy) {
    on_released(sx, sy);
}

}  // namespace noted::app::input
