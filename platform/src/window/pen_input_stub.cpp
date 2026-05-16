// Non-Windows fallback for the pen-input subsystem.
//
// The cross-platform `Window::create` calls `install_pen_input` unconditionally
// so the wiring path is identical everywhere. On macOS / Linux we currently
// have nothing to install — GLFW already delivers mouse events with
// pressure = 1.0F, and proper digitizer support waits on a follow-up PR
// (NSEvent / libinput).
//
// Returning success here is intentional: install failure would be a hard
// error in `Window::create`, but "no pen-input plumbing on this OS" is
// not a failure. It's just less data.

#include "noted/platform/window/pen_input.hpp"

namespace noted::platform::pen {

auto install_pen_input(void* /*native_handle*/) -> Result<void> {
    return {};
}

void uninstall_pen_input(void* /*native_handle*/) noexcept {
    // intentionally empty
}

}  // namespace noted::platform::pen
