#pragma once

// Mac-style window chrome — traffic-light buttons (close / minimise /
// maximise) + centred title + drag region.
//
// **Phase 2 of ADR 0034.** Replaces the OS-native title bar when the
// platform Window is created with `WindowDesc::frameless = true`. The
// chrome is drawn each frame at the very top of the swapchain via an
// always-on ImGui window — that keeps it inside the same Vulkan
// canvas the engine renders, so a frameless QWindow does not need a
// second Qt widget overlay just to provide the title bar.
//
// The widget is pure-presentation: it returns a `MacChromeResult`
// describing which button (if any) the user clicked plus whether the
// user pressed inside the drag region. The host decides what to do
// (typically: forward to `platform::Window::minimize` /
// `toggle_maximize` / `request_close` / `start_system_drag`).
//
// Visual specification (matches macOS Sonoma):
//   - Strip height: 28 px (DPI-aware via ImGui's CurrentStyle font size).
//   - Background: subtle gradient or solid neutral — matches the
//     surrounding theme; see `ui::theme`.
//   - Traffic lights at 18 px from the left edge:
//       red close   (#ED6A5E) — diameter 12 px
//       yellow min  (#F5BF4F) — 6 px gap, diameter 12 px
//       green max   (#62C554) — 6 px gap, diameter 12 px
//   - Title text: centred horizontally, monospace neutral grey.
//   - Drag region: every pixel of the strip outside the three
//     buttons, on the left mouse button.

#include <string_view>

namespace noted::ui::widget {

struct MacChromeResult {
    bool close_clicked{false};
    bool minimize_clicked{false};
    bool maximize_clicked{false};
    // True when the user pressed the left mouse button inside the
    // drag region (= strip area minus the three buttons). The host
    // forwards this to `platform::Window::start_system_drag` which
    // hands off to the OS to perform the rest of the drag — no
    // continuous follow-up call is required.
    bool drag_started{false};
};

// Draws the Mac-style chrome strip. Must be called once per frame
// **before** any other ImGui window so the chrome paints in front
// of everything else on the strip's Z-band. The first parameter is
// the title text rendered centred; the second is the window's
// "maximised?" predicate (used to swap the maximise button glyph).
//
// Coordinate system: places itself at `ImGui::SetNextWindowPos(0, 0)`
// with width = the current main viewport's width. Caller does not
// need to pre-position anything.
[[nodiscard]] auto draw_mac_chrome(std::string_view title, bool is_maximized) -> MacChromeResult;

// Height in **logical pixels** the chrome reserves at the top of
// the swapchain. Callers shift the rest of their layout down by
// this amount so panels don't overlap the chrome.
[[nodiscard]] auto mac_chrome_height_px() -> float;

}  // namespace noted::ui::widget
