#pragma once

// ImGuiInputBridge — feeds ImGui IO from the engine's hook registry
// instead of via a platform-specific ImGui backend.
//
// **Phase 1 of ADR 0034.** Replaces `imgui_impl_glfw` with a thin,
// platform-agnostic shim: every pointer / scroll / key event flowing
// through `noted::hook::registry()` is forwarded into `ImGui::GetIO()`.
// The same hook registry feeds the camera controller, stroke engine
// and tool router, so ImGui sees the same input the rest of the app
// does — single source of truth.
//
// What this bridge handles:
//   - Pointer position, button press/release, scroll wheel.
//   - Common modifier keys (Ctrl / Shift / Alt) — required for
//     ImGui's keyboard-chord handling (Ctrl+Z etc.).
//   - Letter / digit keys that appear in our keyboard shortcuts.
//
// What this bridge does NOT handle (yet):
//   - Clipboard glue. ImGui's default clipboard implementation
//     (system-provided on Windows / X11) is fine for now.
//   - Mouse-cursor shape changes. ImGui's `MouseCursorShape` flag
//     would need a `QGuiApplication::setOverrideCursor` mapping;
//     not visible at the v0.x UX level.
//   - IME composition events. Re-introduce when Korean / CJK
//     typing inside ImGui InputText becomes a real surface (we
//     mostly drive text input via the menu bar / hotkeys today).
//
// Lifetime: heap-allocate via `create()` (the bridge captures
// `this` in its hook lambdas, so the address must be stable).
// `ImGuiHost` owns one instance for the whole UI lifetime.

#include <memory>

#include "noted/engine/error/error.hpp"

namespace noted::ui {

class ImGuiInputBridge {
public:
    [[nodiscard]] static auto create() -> std::unique_ptr<ImGuiInputBridge>;

    ImGuiInputBridge(const ImGuiInputBridge&) = delete;
    auto operator=(const ImGuiInputBridge&) -> ImGuiInputBridge& = delete;
    ImGuiInputBridge(ImGuiInputBridge&&) = delete;
    auto operator=(ImGuiInputBridge&&) -> ImGuiInputBridge& = delete;
    ~ImGuiInputBridge();

    // Called once per frame from the host's `begin_frame` BEFORE
    // ImGui::NewFrame. Refreshes display size + delta time on the
    // ImGui IO — the values ImGui needs that don't arrive via
    // input events.
    void new_frame(float framebuffer_w_px, float framebuffer_h_px, float delta_seconds) noexcept;

private:
    ImGuiInputBridge() noexcept;
    void install_();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace noted::ui
