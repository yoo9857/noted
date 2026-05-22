#pragma once

// Brush library panel — Photoshop / Procreate-style brush picker.
//
// Shows a grid of preset cards above the brush-options sliders. Each
// card renders a LIVE stamp-curve preview of the preset (so the user
// SEES the difference between "Soft Pencil" and "Marker" without
// having to apply each one), highlights the active preset, and
// surfaces save / delete intents back to the host as a
// `BrushLibraryAction`. The host (App) executes those intents
// against the actual library + persists the result.
//
// Pure ImGui — no engine / domain dependency beyond `BrushPreset` /
// `BrushLibrary` headers. The previews draw via `ImDrawList`:
//   * an arc/S-curve sample path,
//   * stamps placed every preset-spacing along it,
//   * stamp colour + size + opacity taken straight from the preset
//     so the swatch reads as a believable stroke sample.

#include <cstddef>
#include <cstdint>
#include <string>

#include "noted/domain/tool/brush_preset.hpp"

namespace noted::domain::tool {
class BrushLibrary;
struct PenOptions;
}  // namespace noted::domain::tool

namespace noted::ui::widget {

// Action the panel surfaces back to the host this frame. At most ONE
// per frame.
struct BrushLibraryAction {
    enum class Kind : std::uint8_t {
        none = 0,
        // User clicked a preset card — host should
        // `apply_preset_to(library.find(id), pen_options)`.
        apply = 1,
        // User clicked "Save current as preset" — host should add a
        // new preset whose stroke-shape fields mirror the current
        // PenOptions, with `kind` defaulting to BrushKind::pen and
        // the name picked from `save_name` (a "Untitled brush N"
        // unique default).
        save_current = 2,
        // User clicked the trash button on a card. Host removes the
        // preset + writes the user library back to disk.
        remove = 3,
    };
    Kind kind{Kind::none};
    noted::domain::tool::BrushPresetId preset_id{noted::domain::tool::invalid_brush_preset_id};
    std::string save_name;  // populated when kind == save_current
};

// Render the brush library section. `active_preset_id` is the id of
// the currently-applied preset (host tracks it across frames); pass
// `invalid_brush_preset_id` when no preset is "current" (e.g. user
// hand-edited the sliders since the last apply). The host may use
// the returned action to mutate the library.
[[nodiscard]] auto brush_library_panel(const noted::domain::tool::BrushLibrary& library,
                                       const noted::domain::tool::PenOptions& current_pen,
                                       noted::domain::tool::BrushPresetId active_preset_id)
    -> BrushLibraryAction;

}  // namespace noted::ui::widget
