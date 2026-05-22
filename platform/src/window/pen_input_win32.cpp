// Windows pen-input implementation.
//
// Strategy:
//   1. Install a window subclass on the GLFW window's HWND so we receive
//      messages BEFORE GLFW's own WndProc runs.
//   2. Translate WM_POINTERDOWN / WM_POINTERUPDATE / WM_POINTERUP for
//      PT_PEN pointers into PointerPressed / PointerMoved / PointerReleased
//      hook events with real pressure and tilt.
//   3. Swallow synthetic WM_MOUSE* messages that Windows generates from
//      pen / touch events so GLFW doesn't deliver a second, pressure-less
//      copy. Detection uses the documented MI_WP_SIGNATURE marker on
//      GetMessageExtraInfo().
//
// Non-pen pointer events (PT_TOUCH, PT_MOUSE-through-pointer) currently
// fall through to GLFW. Touch will get its own path in a later PR.
//
// Comctl32 is linked privately to platform/ (see CMakeLists.txt). It ships
// with every Windows SDK and is on every Windows install — no extra
// runtime dependency.

#include "noted/platform/window/pen_input.hpp"

// WIN32_LEAN_AND_MEAN is already set at the project level
// (CompilerWarnings.cmake adds it as a compile_definition); guard the
// local define to avoid C4005 "macro redefinition".
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// clang-format off
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
// clang-format on

#include <string>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::platform::pen {

namespace {

// MI_WP_SIGNATURE marker: bits indicate the synthetic mouse message was
// generated from a pen or touch event. See MSDN "From Mouse to Pointer".
// We use a single mask + match to swallow all such synthetic mouse
// messages — pen events are delivered through WM_POINTER* instead, touch
// is currently ignored.
constexpr LONG_PTR kMiWpSignature = 0xFF515700;
constexpr LONG_PTR kMiWpSignatureMask = 0xFFFFFF80;

// Sub-class ID for SetWindowSubclass. Any uint we own; 0xA001 ("Pen 1")
// won't collide with GLFW's internal subclass.
constexpr UINT_PTR kSubclassId = 0xA001U;

[[nodiscard]] auto map_pen_button(const POINTER_PEN_INFO& pi) -> noted::hook::PointerButton {
    // Eraser-end touching the tablet → "right button" today. Inks the
    // background color in most apps; for our MVP that maps cleanly to
    // the existing right-button semantics. The barrel button is a more
    // app-defined gesture and we leave it as "other" until ADR 0017's
    // pen-button-mapping section gets concrete UX.
    if ((pi.penFlags & PEN_FLAG_ERASER) != 0 || (pi.penFlags & PEN_FLAG_INVERTED) != 0) {
        return noted::hook::PointerButton::right;
    }
    if ((pi.penFlags & PEN_FLAG_BARREL) != 0) {
        return noted::hook::PointerButton::other;
    }
    return noted::hook::PointerButton::left;
}

[[nodiscard]] auto extract_pen_sample(HWND hwnd, const POINTER_PEN_INFO& pi) -> RawPenSample {
    POINT pt = pi.pointerInfo.ptPixelLocation;
    ::ScreenToClient(hwnd, &pt);
    return RawPenSample{
        .client_x = static_cast<double>(pt.x),
        .client_y = static_cast<double>(pt.y),
        .pressure_0_1024 = pi.pressure,
        .tilt_x_deg = pi.tiltX,
        .tilt_y_deg = pi.tiltY,
    };
}

void publish_pen_pressed(const POINTER_PEN_INFO& pi, const RawPenSample& s) {
    const auto m = normalize(s);
    noted::hook::registry().on_pointer_pressed.publish(noted::hook::PointerPressed{
        .x = m.x,
        .y = m.y,
        .button = map_pen_button(pi),
        .pressure = m.pressure,
        .tilt_x = m.tilt_x,
        .tilt_y = m.tilt_y,
    });
}

void publish_pen_moved(const RawPenSample& s) {
    noted::hook::registry().on_pointer_moved.publish(normalize(s));
}

void publish_pen_released(const POINTER_PEN_INFO& pi, const RawPenSample& s) {
    const auto m = normalize(s);
    noted::hook::registry().on_pointer_released.publish(noted::hook::PointerReleased{
        .x = m.x,
        .y = m.y,
        .button = map_pen_button(pi),
    });
}

[[nodiscard]] auto is_synthetic_mouse_from_pen_or_touch() noexcept -> bool {
    return (::GetMessageExtraInfo() & kMiWpSignatureMask) == kMiWpSignature;
}

LRESULT CALLBACK subclass_proc(HWND hwnd,
                               UINT msg,
                               WPARAM wparam,
                               LPARAM lparam,
                               UINT_PTR /*uIdSubclass*/,
                               DWORD_PTR /*dwRefData*/) {
    switch (msg) {
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            const UINT32 pointer_id = GET_POINTERID_WPARAM(wparam);
            POINTER_INPUT_TYPE type = PT_POINTER;
            if (::GetPointerType(pointer_id, &type) == 0 || type != PT_PEN) {
                break;  // not pen — fall through to DefSubclassProc
            }
            POINTER_PEN_INFO pi{};
            if (::GetPointerPenInfo(pointer_id, &pi) == 0) {
                break;
            }
            const auto sample = extract_pen_sample(hwnd, pi);
            switch (msg) {
                case WM_POINTERDOWN:
                    publish_pen_pressed(pi, sample);
                    break;
                case WM_POINTERUPDATE:
                    publish_pen_moved(sample);
                    break;
                case WM_POINTERUP:
                    publish_pen_released(pi, sample);
                    break;
                default:
                    break;  // unreachable; outer switch already filtered
            }
            return 0;  // handled — do not forward to GLFW
        }

        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            // Windows generates these from pen and touch events as a
            // courtesy to legacy apps. We handle pen via WM_POINTER*;
            // delivering a second copy through GLFW would duplicate the
            // event with pressure clamped to the default 1.0F.
            if (is_synthetic_mouse_from_pen_or_touch()) {
                return 0;
            }
            break;

        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

auto install_pen_input(void* native_handle) -> Result<void> {
    auto* hwnd = static_cast<HWND>(native_handle);
    if (hwnd == nullptr || ::IsWindow(hwnd) == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "pen::install_pen_input: native_handle is not a valid HWND"));
    }
    if (::SetWindowSubclass(hwnd, &subclass_proc, kSubclassId, 0) == 0) {
        const auto code = ::GetLastError();
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            std::string{"pen::install_pen_input: SetWindowSubclass failed (GetLastError="} +
                std::to_string(code) + ")"));
    }
    return {};
}

void uninstall_pen_input(void* native_handle) noexcept {
    auto* hwnd = static_cast<HWND>(native_handle);
    if (hwnd == nullptr || ::IsWindow(hwnd) == 0) {
        return;
    }
    // Idempotent — RemoveWindowSubclass returns FALSE if the subclass was
    // never installed, which we treat as success.
    (void) ::RemoveWindowSubclass(hwnd, &subclass_proc, kSubclassId);
}

}  // namespace noted::platform::pen
