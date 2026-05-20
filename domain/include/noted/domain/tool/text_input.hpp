#pragma once

// Text-tool data — pure data + a tiny bit of clamping logic.
//
// Phase B.6 of the unified-canvas plan. Unlike the drag-based tools
// (Pen / Eraser / Selection / Shape), the Text tool's interaction
// model is **click + type**: a single press anchors the editing
// position, an ImGui InputText collects keystrokes between frames,
// and Enter (or focus-loss) commits the buffer into a
// `TextPrimitive`.
//
// What's here:
//   - `TextOptions` — user-tweakable parameters bound to UI sliders /
//     colour-pickers on `ToolState::text`. Defaults to 16 px opaque
//     black.
//   - `TextPrimitive` — committed text. Position + content + visual
//     parameters snapshotted at commit time. Same discipline as
//     `ShapePrimitive` from Phase B.5 — slider tweaks never
//     retroactively repaint already-committed text.
//
// What's NOT here (deliberate):
//   - Font picker. v0.x uses ImGuiHost's loaded CJK font for
//     everything; future PR could expose per-tool font selection
//     once a font registry exists.
//   - Multi-line / word-wrap. v0.x is single-line — the InputText
//     widget collects keys until Enter. Multi-line lands once a
//     real text renderer (e.g. HarfBuzz + a glyph atlas) replaces
//     ImGui's draw-list text.
//   - Persistence to `.noted`. Same story as Phase B.5 shapes — App-
//     owned vector for now; future PR promotes to
//     `Document::texts()` with Commands.

#include <cstddef>
#include <cstdint>
#include <string>

namespace noted::domain::tool {

// User-tweakable text parameters. Lives on `ToolState::text`; the
// `brush_options` widget renders a font-size slider + colour picker
// that mutates these in place. Snapshotted into `TextPrimitive` at
// commit time.
struct TextOptions {
    // Pixel size of the rendered glyphs. 8 px is the smallest still-
    // legible value at 1× zoom on most displays; 200 px is the
    // largest the brush_options slider exposes (purely a UX cap —
    // nothing in the renderer breaks at larger values).
    float font_size_px{16.0F};

    // Straight-alpha colour. Default = opaque black.
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};

    [[nodiscard]] auto operator==(const TextOptions&) const noexcept -> bool = default;
};

// One committed text — what `text_overlay` renders + what a future
// `.noted` writer would persist. All fields are values; trivially
// copyable; cheap to store in a `std::vector`.
struct TextPrimitive {
    // Top-left anchor in canvas pixels (matches ImGui's text
    // baseline convention — top-left of the bounding box, NOT the
    // typographic baseline).
    double x{0.0};
    double y{0.0};
    std::string content;

    // Visual parameters snapshotted from `TextOptions` at commit
    // time. Carried per-primitive so a mid-document slider tweak
    // doesn't repaint history.
    float font_size_px{16.0F};
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};

    [[nodiscard]] auto is_empty() const noexcept -> bool { return content.empty(); }

    [[nodiscard]] auto operator==(const TextPrimitive&) const noexcept -> bool = default;
};

// In-flight editing session. Pure data so it can live in `domain`
// and be shared between the app-layer handler (writes position +
// options, calls commit/cancel) and the ui-layer overlay (reads
// position, writes `buffer` via ImGui InputText). Carrying it here
// keeps `ui` free of any `app/` dependency — the overlay only needs
// domain types + a couple of callbacks.
struct TextEditingState {
    double position_x{0.0};
    double position_y{0.0};

    // ImGui InputText writes into a fixed buffer. 1 KiB is plenty for
    // a v0.x single-line label; multi-line lands on a different path
    // entirely (real text renderer + glyph atlas).
    static constexpr std::size_t kBufferCapacity = 1024;
    char buffer[kBufferCapacity]{};

    // Snapshotted at PRESS so mid-typing slider tweaks don't
    // retroactively repaint the in-flight glyphs.
    TextOptions options{};

    // Set to true on the first frame after editing begins so the
    // overlay calls `ImGui::SetKeyboardFocusHere()` once. The overlay
    // clears it after consuming.
    bool needs_focus{false};
};

// Build a `TextPrimitive` from a commit position + the editing
// buffer + the snapshotted options. Strips leading/trailing
// whitespace and returns `false`-equivalent (empty content) when
// nothing is left — the handler discards empty commits so a
// stray click doesn't litter the canvas with zero-character text.
//
// Font size clamped to a 1 px floor; NaN / negative collapse to 1 px.
[[nodiscard]] auto text_primitive_from(double x,
                                       double y,
                                       std::string content,
                                       const TextOptions& opt) noexcept -> TextPrimitive;

}  // namespace noted::domain::tool
