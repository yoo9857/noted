#pragma once

// Tool — the active editing tool the user has selected.
//
// Phase B.1 of the unified-canvas plan. The tool palette in the UI
// flips `ToolState::active`; the StrokeEngine (and future shape /
// selection / text tools) read it each frame to decide their
// behaviour.
//
// **Pure data.** Per-tool option payloads (brush options for pen, hardness
// for eraser, shape kind, font for text, ...) will land alongside their
// behavioural integration in later PRs. Today only `active` is here so
// the surface stays small.
//
// Wire-stable enum ordinals — `.noted` v3 will persist the active
// tool. New kinds append; existing values never reorder.

#include <cstdint>
#include <string_view>

#include "noted/domain/tool/options.hpp"

namespace noted::domain::tool {

enum class ToolKind : std::uint8_t {
    pen = 0,     // freehand vector ink — the default tool
    eraser = 1,  // removes ink (B.1: paints with paper colour as a
                 // visual placeholder; proper destination-out
                 // semantics land in B.2 once the stroke layer is
                 // split from the page-background layer).
    select = 2,  // rectangle / lasso selection (B.x)
    shape = 3,   // primitive shapes (B.x)
    text = 4,    // text insertion (B.x)
    image = 5,   // image placement (B.x)
};

inline constexpr ToolKind kFirstTool = ToolKind::pen;
inline constexpr ToolKind kLastTool = ToolKind::image;
inline constexpr std::size_t kToolCount = 6;

struct ToolState {
    ToolKind active{ToolKind::pen};

    // Per-tool option payloads. Stored side-by-side rather than inside
    // a `std::variant` so the inactive tools' state persists across
    // switches — flipping from Pen to Eraser and back should not
    // forget the Pen's colour. Memory cost is a handful of floats
    // per tool, well worth the ergonomic win.
    //
    // Select / shape / text / image will gain their own option
    // structs as their behavioural integrations land (Phase B.4+).
    PenOptions pen{};
    EraserOptions eraser{};

    [[nodiscard]] auto operator==(const ToolState&) const noexcept -> bool = default;
};

// Human-readable label for a ToolKind. Stable strings — the palette
// widget renders these as button labels and a future Preferences UI
// can expose them in keybinding lists.
[[nodiscard]] auto label(ToolKind k) noexcept -> std::string_view;

}  // namespace noted::domain::tool
