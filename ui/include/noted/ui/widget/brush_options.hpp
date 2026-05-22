#pragma once

// Brush options — the active tool's runtime-tweakable controls.
//
// Renders ImGui widgets bound to the per-tool option payload on the
// caller-owned `ToolState`. The widget is **stateless** but mutates
// the state in place (the ImGui idiom for sliders / colour pickers
// — `ImGui::SliderFloat(&value)` writes through the pointer).
//
// Which controls show depends on `ToolState::active`:
//   - Pen    → size range (min / max radius), alpha gamma, colour
//   - Eraser → size range, alpha gamma
//   - Shape  → kind / stroke width / stroke colour
//   - Text   → font size / colour
//   - Image  → width / height / tint + "Pick image…" button + current asset label
//   - Select → placeholder
//
// Slider edits + colour picks mutate `tools` directly (live ergonomic
// state, no undo entry). The "Pick image…" button cannot mutate
// state itself — the file dialog needs OS reach + Document mutation
// — so the widget returns a `BrushOptionsResult` flagging the click;
// the App-side glue runs the picker and updates `tools.image` +
// `Document::image_assets()` in response.

#include <cstdint>
#include <string>

#include "noted/domain/document/image_asset_registry.hpp"
#include "noted/domain/tool/brush_preset.hpp"
#include "noted/domain/tool/tool.hpp"

namespace noted::domain::tool {
class BrushLibrary;
}  // namespace noted::domain::tool

namespace noted::ui::widget {

// Per-frame outcome of `brush_options`. The widget never mutates
// state that lives outside `ToolState`; anything App-level (file
// dialogs, Document mutations, library mutations + disk writes) is
// signalled here.
struct BrushOptionsResult {
    bool pick_image_requested{false};

    // Brush-library intents — at most ONE per frame. The brush
    // picker grid is folded INTO the brush_options window scope
    // (single ImGui::Begin per frame) so all results coalesce here.
    enum class LibraryAction : std::uint8_t {
        none = 0,
        apply = 1,
        save_current = 2,
        remove = 3,
    };
    LibraryAction library_action{LibraryAction::none};
    noted::domain::tool::BrushPresetId library_preset_id{
        noted::domain::tool::invalid_brush_preset_id};
    std::string library_save_name;  // populated on save_current
};

// Render the brush options inside a SINGLE ImGui::Begin / End scope
// managed by this function. `open` controls visibility — pass
// `&state.show_brush_options`. The Pen / Eraser / etc. slider
// section is preceded by the brush-library picker grid when
// `library` is non-null AND the active tool is the Pen — Eraser /
// Shape / etc. don't share the Pen's preset space.
//
// `image_assets` lets the Image-tool section render the source
// filename for the currently-picked asset.
[[nodiscard]] auto brush_options(noted::domain::tool::ToolState& tools,
                                 const noted::domain::ImageAssetRegistry& image_assets,
                                 const noted::domain::tool::BrushLibrary* library,
                                 noted::domain::tool::BrushPresetId active_preset_id,
                                 bool* open) -> BrushOptionsResult;

}  // namespace noted::ui::widget
