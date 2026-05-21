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

#include "noted/domain/document/image_asset_registry.hpp"
#include "noted/domain/tool/tool.hpp"

namespace noted::ui::widget {

// Per-frame outcome of `brush_options`. The widget never mutates
// state that lives outside `ToolState`; anything App-level (file
// dialogs, Document mutations) is signalled here.
struct BrushOptionsResult {
    // User clicked "Pick image…" this frame. App glue should invoke
    // `platform::io::pick_image_open()` + `image_io::load_rgba8` +
    // `Document::image_assets_mut().allocate(...)` and write the
    // resulting AssetId + intrinsic dimensions back onto
    // `tools.image`.
    bool pick_image_requested{false};
};

// Render the brush options inside an ImGui::Begin / End scope managed
// by this function. `open` controls visibility — pass
// `&state.show_brush_options` from MenuBarState. `tools` is mutated
// in place when the user drags a slider / picks a colour.
//
// `image_assets` lets the Image-tool section render the source
// filename for the currently-picked asset (`tools.image.pending_asset_id`).
// Pass the document's read-only registry; an empty registry renders
// the "(no image picked)" placeholder.
[[nodiscard]] auto brush_options(noted::domain::tool::ToolState& tools,
                                 const noted::domain::ImageAssetRegistry& image_assets,
                                 bool* open) -> BrushOptionsResult;

}  // namespace noted::ui::widget
