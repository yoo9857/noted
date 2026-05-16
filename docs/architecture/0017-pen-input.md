# ADR 0017: Pen / stylus input via Windows Pointer API

**Status:** Accepted
**Date:** 2026-05-16

## Context

GLFW reports pointer events as mouse events with `pressure = 1.0F`
hardcoded. The stroke engine already wires `pressure` through to the
stamp's alpha (see ADR 0015), so the path from digitizer-to-pixel
exists — but every stroke comes out at full opacity because there is
no real pressure source.

To make the app feel like a real note-taker we need:

1. **Real pressure** — 0..1024 from the digitizer, mapped to [0, 1].
2. **Real tilt** — `tiltX` / `tiltY` in degrees, for future bristle /
   chisel brushes.
3. **Per-pen-button mapping** — eraser end → right-button semantics
   so the existing `PointerButton` enum continues to do useful work.
4. **No double events** — Windows synthesizes WM_MOUSE* messages from
   pen events for legacy apps; GLFW would otherwise deliver both the
   real pen event AND a pressure-less mouse event for the same touch.

GLFW does not expose any of this. Three options:

- **Wait for upstream GLFW** to land the pointer events from the
  open RFC. No timeline; the RFC has been open for years.
- **Replace GLFW** with our own windowing layer. Multi-month
  undertaking. Rejected for now.
- **Augment GLFW** by intercepting Windows messages before they
  reach GLFW's WndProc.

We go with the augmentation route. This ADR specifies how.

## Decision

### Public API — cross-platform stub + Win32 impl

```cpp
namespace noted::platform::pen {

struct RawPenSample {
    double         client_x;
    double         client_y;
    std::uint32_t  pressure_0_1024;
    std::int32_t   tilt_x_deg;
    std::int32_t   tilt_y_deg;
};

[[nodiscard]] auto normalize(const RawPenSample&)
    -> noted::hook::PointerMoved;

[[nodiscard]] auto install_pen_input(void* native_handle)
    -> Result<void>;
void uninstall_pen_input(void* native_handle) noexcept;

}  // namespace noted::platform::pen
```

- `normalize` is **pure** — testable on every host, no Win32 dep. It
  encodes the pressure clamp ([0, 1024] → [0, 1]) and tilt
  pass-through. The Win32 TU calls it; the unit tests call it.
- `install_pen_input` / `uninstall_pen_input` take `void*` so the
  cross-platform header doesn't drag in `<windows.h>`. The Win32
  impl casts to `HWND` internally.
- On macOS / Linux the stub returns success and does nothing. Future
  NSEvent / libinput work plugs in without changing the contract.

### Win32 implementation — `SetWindowSubclass`

After `glfwCreateWindow`, `Window::create` calls `install_pen_input
(glfwGetWin32Window(h))`. The Win32 implementation calls
`SetWindowSubclass` (Comctl32) to insert our `WndProc` ahead of
GLFW's. The subclass:

1. **Intercepts `WM_POINTERDOWN` / `WM_POINTERUPDATE` / `WM_POINTERUP`.**
   For each, it pulls the pointer ID via `GET_POINTERID_WPARAM`,
   checks the type via `GetPointerType`, and only acts on `PT_PEN`
   (non-pen pointers fall through to GLFW unchanged). For pen
   pointers it calls `GetPointerPenInfo` to extract pressure / tilt /
   screen-space coords, `ScreenToClient`-converts the coords, and
   publishes the matching `PointerPressed` / `PointerMoved` /
   `PointerReleased` hook event.

2. **Swallows synthetic `WM_MOUSE*` events** that Windows generates
   from pen / touch input. Detection uses the documented
   `MI_WP_SIGNATURE` marker (`0xFF515700`) on `GetMessageExtraInfo()`
   with mask `0xFFFFFF80`. Synthetic = swallow (return 0); real mouse
   = forward to GLFW.

3. **Maps pen flags to PointerButton.** Eraser end (`PEN_FLAG_ERASER`
   or `PEN_FLAG_INVERTED`) → right; barrel button (`PEN_FLAG_BARREL`)
   → other; default tip touch → left. The UX of "barrel does what" is
   parked at `other` until we have brush settings to bind to it.

### Coordinate space

`POINTER_PEN_INFO::pointerInfo.ptPixelLocation` is screen pixels.
`ScreenToClient` converts to window-client pixels. Because the app
is DPI-aware (GLFW 3.3+ sets `PerMonitorV2` awareness), client
pixels equal framebuffer pixels — the same space the existing mouse
events occupy after `window_to_framebuffer()`. So pen and mouse
events share a coordinate system without extra scaling.

### Failure handling

`install_pen_input` returns `Result<void>`. On failure we publish an
`ErrorObserved{recoverable = true}` event and continue — pen support is
an enhancement, not a precondition. The app stays usable on a mouse
even if subclassing somehow fails (it won't, on a real GUI thread).

## Alternatives considered

- **`EnableMouseInPointer(TRUE)`** — unifies all pointer input
  through `WM_POINTER*`, no `WM_MOUSE*` arrives at GLFW. Cleaner
  conceptually, but every mouse event then has to be reconstructed
  from the pointer info and re-delivered to GLFW, doubling the
  surface area. Rejected for MVP.
- **Use `glfwSetCursorPosCallback` and read pressure separately via
  RealTimeStylus or HID** — `RTS` is the old (Vista-era) WPF stack
  and a separate process boundary. HID is closer to the metal but
  requires raw input setup and per-device parsing. The Pointer API
  is the modern, documented answer.
- **Subclass via `SetWindowLongPtr(GWLP_WNDPROC)`** — works, but a
  raw WndProc swap conflicts with anyone else who installs a subclass
  (debuggers, accessibility tooling). `SetWindowSubclass` was
  designed exactly to compose, so use it.
- **Install before GLFW (e.g., via `SetWindowsHookEx`)** — global
  hook, requires per-process registration, anti-virus alarms. We only
  need per-window subclass.

## Consequences

- The stroke engine's pressure path becomes meaningful on Windows.
  Today's MVP uses `e.pressure * default_alpha` (ADR 0015); when
  `feat/stroke-engine-pressure` (P2 #6) lands, it modulates both
  alpha AND radius from the same input.
- `platform::Window` now installs / uninstalls a system subclass.
  Lifetime is tied to the GLFW window — `Window::destroy` calls
  `uninstall_pen_input` before `glfwDestroyWindow`.
- The cross-platform contract is preserved. Linux and macOS builds
  see a no-op stub; the rest of the engine has no `#ifdef _WIN32`
  in its hot path.
- `platform/` links `Comctl32` privately on Windows. Available on
  every Windows install; no shipping headache.
- Unit tests cover the pure `normalize` translation on every host
  (boundary pressures, tilt forwarding, coordinate pass-through,
  overflow clamping, old-hardware dynamic range).

## Follow-ups

- **macOS NSEvent path** — `[NSEvent pressure]`, `[NSEvent
  tiltX/tiltY]` exposed through a thin Obj-C++ TU compiled only
  on Apple.
- **Linux libinput** — Wayland Wacom support via libinput's
  `tablet_tool_*` API. X11 may need XInput2 separately.
- **Hover state** — pen hovering over the tablet without contact
  publishes `PointerMoved` with `pressure = 0`. Useful for brush
  preview / cursor rendering but skipped in MVP.
- **Pen barrel button UX** — currently `PointerButton::other`; bind
  to "color picker" or "eraser" once the brush UI exists.
- **Touch path** — `PT_TOUCH` events flow through `WM_POINTER*`
  too. Land alongside multi-touch gesture work in a later PR.
