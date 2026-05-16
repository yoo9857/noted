#pragma once

// Pen / stylus input.
//
// The platform's stylus stack publishes the same `PointerPressed` /
// `PointerMoved` / `PointerReleased` hook events as the mouse path, but
// the `pressure` and `tilt_x` / `tilt_y` fields carry real values from
// the digitizer instead of the GLFW default of `pressure = 1.0F`.
//
// On Windows the implementation subclasses the GLFW window's HWND and
// translates WM_POINTER* messages whose pointer type is PT_PEN. Synthetic
// WM_MOUSE* messages that Windows generates from pen input are detected
// via the `MI_WP_SIGNATURE` (0xFF515700) extra-info marker and swallowed
// inside the subclass so GLFW sees no duplicate events.
//
// On macOS and Linux this is a no-op today; future work tracked under
// [[reference-roadmap]] will fold in NSEvent (Apple Pencil / Wacom) and
// libinput (Wacom on Wayland) without changing this header's contract.
//
// Rationale: see docs/architecture/0017-pen-input.md.

#include <cstdint>

#include "noted/engine/error/error.hpp"
#include "noted/engine/hook/hook.hpp"

namespace noted::platform::pen {

// Raw digitizer sample, in window/client coordinate space.
//
// `pressure_0_1024` is the unmodified value Windows exposes via
// POINTER_PEN_INFO::pressure. Modern pens use the full 0..1024 range; older
// hardware may cap at 256/512. We always divide by 1024.0 on the way out
// so the normalized `pressure` is consistent across devices.
//
// `tilt_x_deg` / `tilt_y_deg` are the values Windows exposes in
// POINTER_PEN_INFO::tiltX / tiltY (-90..+90 degrees). For pens that do
// not report tilt, Windows reports zero — that's the same default the
// hook payload uses, so callers see no jump when an old pen connects.
struct RawPenSample {
    double client_x{0.0};
    double client_y{0.0};
    std::uint32_t pressure_0_1024{0};
    std::int32_t tilt_x_deg{0};
    std::int32_t tilt_y_deg{0};
};

// Pure translation — testable everywhere, no Win32 dependency.
//
// Produces a PointerMoved with the digitizer's pressure normalized to
// [0, 1] and tilt forwarded as float degrees. Out-of-range
// `pressure_0_1024` values are clamped so the consumer never sees > 1.
[[nodiscard]] auto normalize(const RawPenSample& s) noexcept -> noted::hook::PointerMoved;

// Attach the pen-input pipeline to the platform's native window handle.
//
// `native_handle` is a `void*` so this header doesn't drag in <windows.h>
// or platform-specific opaque types. On Win32 it's the `HWND` returned
// by `glfwGetWin32Window`. On non-Windows targets it is currently
// unused and the call returns success without side effects.
//
// Returns `invalid_state` on Win32 if the subclass installation fails
// (in practice never seen on a real GUI thread — the API is more
// pessimistic than reality).
[[nodiscard]] auto install_pen_input(void* native_handle) -> Result<void>;

// Reverse of install_pen_input. Safe to call on a handle that was never
// installed (no-op).
void uninstall_pen_input(void* native_handle) noexcept;

}  // namespace noted::platform::pen
