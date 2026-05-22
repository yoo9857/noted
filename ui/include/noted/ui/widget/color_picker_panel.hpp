#pragma once

// Color picker panel — large, always-visible right-side floating
// panel for picking the active tool's stroke colour. Replaces the
// small ColorEdit4 squeezed inside the brush_options panel with a
// proper Photoshop / OpenCanvas-style colour surface:
//
//   - Full HSV picker with a hue ring + saturation/value square.
//   - Hex input.
//   - Recent-colours swatch row (8 last-used colours).
//   - Reset to default-black button.
//
// The widget is pure-presentation: it operates on a 4-float RGBA
// array (the caller supplies a pointer into the active tool's
// options struct), reports changes through the return value, and
// owns its own ImGui window scope so the host does no layout.
//
// Layout: anchored to the right edge of the main viewport at
// `top_offset_px`, fixed width, height auto-sized by ImGui.

#include <array>

namespace noted::ui::widget {

struct ColorPickerResult {
    // True when the user moved the picker / typed a hex / clicked a
    // swatch. The caller pushes the latest value into the active
    // tool's options.
    bool changed{false};
    // True when the user committed a colour (mouse released after
    // dragging the picker, or pressed Enter in the hex input). The
    // host pushes the current colour into the palette (auto-save).
    bool committed{false};
    // True when the user explicitly pressed the "+" button — they
    // want the current colour saved as a palette entry even though
    // no fresh commit happened. The host treats this exactly like
    // `committed` (push onto palette + dedup the head).
    bool palette_add_requested{false};
    // Set to the index when the user right-clicked a palette slot
    // to delete it. The host should clear `palette[*delete_index]`
    // (set alpha = 0 → placeholder). `-1` = no delete this frame.
    int palette_delete_index{-1};
};

// Total slots on the artist's palette. 4 columns × 5 rows = 20 —
// roughly matches a physical wooden palette's well count and fits
// without scrolling at the default panel height. Pinned here so the
// App-side storage array and the panel widget share one source of
// truth.
inline constexpr std::size_t kPaletteSlotCount = 20U;

// Render the colour picker panel. `rgba` is the persistent storage
// for the colour (typically `&tools_.pen.r`); the picker mutates it
// in place. The 20-slot `palette_colors` array is owned by the host
// (App keeps it alive across frames); the widget reads/applies
// swatches and surfaces add / delete intents back via
// `ColorPickerResult`.
[[nodiscard]] auto color_picker_panel(
    float* rgba_xyzw,
    const std::array<std::array<float, 4>, kPaletteSlotCount>& palette_colors,
    float top_offset_px,
    float panel_width_px = 220.0F) -> ColorPickerResult;

}  // namespace noted::ui::widget
