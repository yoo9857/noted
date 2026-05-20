#pragma once

// ToolInputHandler — input behaviour for a single editing tool.
//
// Phase R.1 of the App-layer decomposition (per ADR 0032). The
// `ToolInputRouter` dispatches left-button pointer events to the
// handler whose `handled_kind()` matches the active tool. Each tool
// (Select / Shape / Text / Image / ...) ships its own subclass that
// owns its in-flight drag state — App stops growing a fresh trio of
// pointer-event lambdas per tool.
//
// Coordinates are **canvas-space** (already unprojected through the
// camera by the router). Handlers operate in document pixels, not
// screen pixels, so a mid-drag pan / zoom doesn't desync the drag.
//
// Lifetime: handlers are owned by the router; the router holds them
// as `std::unique_ptr<ToolInputHandler>` and dispatches via the
// abstract interface. Handlers may capture references to other App
// state (e.g. `Selection&`) in their constructors — App outlives the
// router which outlives the handlers, so the references stay valid.

namespace noted::domain::tool {
enum class ToolKind : unsigned char;
}  // namespace noted::domain::tool

namespace noted::app::input {

class ToolInputHandler {
public:
    ToolInputHandler() = default;
    virtual ~ToolInputHandler() = default;
    ToolInputHandler(const ToolInputHandler&) = delete;
    auto operator=(const ToolInputHandler&) -> ToolInputHandler& = delete;
    ToolInputHandler(ToolInputHandler&&) = delete;
    auto operator=(ToolInputHandler&&) -> ToolInputHandler& = delete;

    // Which tool this handler claims. The router uses this to pick
    // the active handler when `set_active` is called.
    [[nodiscard]] virtual auto handled_kind() const noexcept -> noted::domain::tool::ToolKind = 0;

    // Left-button press. Modifier flags come from ImGui's IO (which
    // reflects GLFW's keyboard state at event time) snapshotted by
    // the router immediately before this call. `shift` / `alt`
    // covers the Photoshop-conventional modifier set; future tools
    // that need Ctrl can extend the signature in a follow-up.
    virtual void on_pressed(double canvas_x, double canvas_y, bool shift, bool alt) = 0;

    // Left-button drag. Called every PointerMoved while the user
    // holds the left button AFTER `on_pressed` was dispatched to
    // this handler. The router doesn't track "is dragging" — it
    // dispatches unconditionally; handlers may early-return when
    // they have no active drag.
    virtual void on_moved(double canvas_x, double canvas_y) = 0;

    // Left-button release. The handler commits or discards whatever
    // drag state it accumulated.
    virtual void on_released(double canvas_x, double canvas_y) = 0;

    // The active tool just switched AWAY from `handled_kind()`. The
    // handler must finalize its in-flight state (commit a partial
    // drag, or discard it — handler's choice). Without this hook,
    // a mid-drag tool switch would dangle drag state until the user
    // came back to this tool.
    virtual void on_deactivated() noexcept = 0;
};

}  // namespace noted::app::input
