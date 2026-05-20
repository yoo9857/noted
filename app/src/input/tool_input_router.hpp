#pragma once

// ToolInputRouter — the App-layer pointer-event dispatcher.
//
// Phase R.1 of the App-layer decomposition (per ADR 0032).
//
// Subscribes once to the hook registry's PointerPressed / Moved /
// Released channels and dispatches each LEFT-button event to the
// `ToolInputHandler` whose `handled_kind()` matches the currently
// active tool. Middle and right button events are ignored (those
// remain owned by the camera-pan + future context-menu code).
//
// Coordinate convention: handlers see **canvas-space** coordinates.
// The router unprojects through the supplied `Camera&` so handlers
// stay independent of the screen-vs-canvas distinction.
//
// Non-movable + heap-allocated (`unique_ptr<ToolInputRouter>` is the
// recommended ownership). Hook subscriptions are RAII-tied to the
// router instance; moving would dangle the captured `this`.

#include <memory>
#include <vector>

#include "noted/domain/tool/tool.hpp"
#include "noted/engine/hook/hook.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::canvas {
class Camera;
}  // namespace noted::canvas

namespace noted::hook {
class Registry;
}  // namespace noted::hook

namespace noted::app::input {

class ToolInputRouter {
public:
    // Subscribes to PointerPressed / Moved / Released. The
    // `camera` reference is used only to unproject screen
    // coordinates into canvas pixels — the router never mutates it.
    [[nodiscard]] static auto create(noted::hook::Registry& registry,
                                     const noted::canvas::Camera& camera)
        -> std::unique_ptr<ToolInputRouter>;

    ToolInputRouter(const ToolInputRouter&) = delete;
    auto operator=(const ToolInputRouter&) -> ToolInputRouter& = delete;
    ToolInputRouter(ToolInputRouter&&) = delete;
    auto operator=(ToolInputRouter&&) -> ToolInputRouter& = delete;
    ~ToolInputRouter() = default;

    // Register a handler. Subsequent `set_active(handler.handled_kind())`
    // routes events to it. Registering two handlers for the same kind
    // is a programming error — only the FIRST wins (router-level
    // `validate` would surface this; for v0.x we just document).
    void register_handler(std::unique_ptr<ToolInputHandler> handler);

    // Activate the handler matching `kind`. The previously-active
    // handler (if any) gets `on_deactivated()` first so it can
    // finalize in-flight state. Passing the currently-active kind
    // is a no-op (no deactivate / activate round-trip).
    void set_active(noted::domain::tool::ToolKind kind);

    // Inspect — used by tests to confirm dispatch state. Returns
    // nullptr when no handler is active (kind unregistered or
    // `set_active` not yet called).
    [[nodiscard]] auto active() const noexcept -> ToolInputHandler* { return active_; }

    // Test-only injection helpers — bypass the hook system so unit
    // tests can dispatch synthetic events. Mirror what the GLFW
    // callbacks do.
    struct TestingTag {};
    explicit ToolInputRouter(TestingTag) noexcept {}
    void inject_press_(double screen_x, double screen_y, bool shift, bool alt);
    void inject_move_(double screen_x, double screen_y);
    void inject_release_(double screen_x, double screen_y);

private:
    ToolInputRouter() = default;

    void on_pressed(double screen_x, double screen_y, bool shift, bool alt);
    void on_moved(double screen_x, double screen_y);
    void on_released(double screen_x, double screen_y);

    std::vector<std::unique_ptr<ToolInputHandler>> handlers_{};
    ToolInputHandler* active_{nullptr};

    // Camera is held by pointer (rather than reference) so the
    // testing path can construct a router with no camera and use
    // identity unprojection. Production calls supply a real camera.
    const noted::canvas::Camera* camera_{nullptr};

    // RAII subscriptions — released when the router is destroyed.
    noted::hook::Subscription<noted::hook::PointerPressed> sub_pressed_{};
    noted::hook::Subscription<noted::hook::PointerMoved> sub_moved_{};
    noted::hook::Subscription<noted::hook::PointerReleased> sub_released_{};
};

}  // namespace noted::app::input
