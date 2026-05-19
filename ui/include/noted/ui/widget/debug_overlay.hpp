#pragma once

// Debug overlay — small floating window with live engine telemetry.
//
// Renders four sections:
//   1. Frame index + ImGui's averaged FPS / ms-per-frame
//   2. A 120-sample rolling CPU-time plot driven by the host
//      pushing `on_frame_end.cpu_ms` values each frame
//   3. Every registered `harness::Counter` (name + value)
//   4. The `LayerCompositor::fallback_count()` from the inputs —
//      counts blend modes that fell back to NORMAL because no
//      fixed-function pipeline implements them (ADR 0019)
//
// Stateless function + caller-owned `DebugOverlayState` for the
// ring buffer — matches the rest of `ui/widget/` (status_bar,
// outline_panel). Toggling lives in MenuBarState (View → Debug
// overlay); off by default.
//
// One-frame lag: the most recent CPU-time sample is from the
// previous frame (engine fires `on_frame_end` AFTER ImGui's frame
// is closed). Standard for any frame-loop profiler — documented
// rather than worked around.

#include <array>
#include <cstddef>
#include <cstdint>

namespace noted::ui::widget {

struct DebugOverlayInputs {
    std::uint64_t frame_index{0};
    // Snapshotted at draw time. `LayerCompositor::fallback_count()`
    // is the canonical source; main.cpp wires the value through.
    std::uint64_t fallback_count{0};
    // Active canvas camera (Goodnotes-style pan + zoom). The host
    // passes values from its `noted::canvas::Camera`. Zero scale
    // suppresses the row — leaves headless / test contexts clean.
    double camera_scale{0.0};
    double camera_translation_x{0.0};
    double camera_translation_y{0.0};
};

// Ring buffer of recent per-frame CPU times. Caller owns one; pushes
// each new sample as `on_frame_end` fires, draws via `debug_overlay`.
//
// Capacity sized for ~2 seconds at 60 Hz — comfortable visual
// scroll-window without spending real memory on history nobody
// looks at. The `head` / `size` pair makes the wrap-around explicit
// instead of relying on modular arithmetic at draw time.
struct DebugOverlayState {
    static constexpr std::size_t kCapacity = 120;
    std::array<float, kCapacity> samples{};
    std::size_t head{0};  // index of the next slot to write
    std::size_t size{0};  // number of valid samples (saturates at kCapacity)

    void push_sample(float cpu_ms) noexcept {
        samples[head] = cpu_ms;
        head = (head + 1) % kCapacity;
        if (size < kCapacity) {
            ++size;
        }
    }
};

// Render the overlay. No-op when `open != nullptr && !*open`.
// `open` is the View-menu toggle pointer; the widget writes false
// into it when the user clicks the window's X.
void debug_overlay(const DebugOverlayInputs& inputs, const DebugOverlayState& state, bool* open);

}  // namespace noted::ui::widget
