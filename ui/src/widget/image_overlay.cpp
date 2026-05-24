#include "noted/ui/widget/image_overlay.hpp"

#include <algorithm>
#include <cstdint>

#include <imgui.h>

#include "noted/domain/document/asset_id.hpp"
#include "noted/domain/tool/image_input.hpp"

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto pack_color(float r, float g, float b, float a) noexcept -> ImU32 {
    const auto clamp01 = [](float v) -> float { return std::clamp(v, 0.0F, 1.0F); };
    return IM_COL32(static_cast<int>(clamp01(r) * 255.0F),
                    static_cast<int>(clamp01(g) * 255.0F),
                    static_cast<int>(clamp01(b) * 255.0F),
                    static_cast<int>(clamp01(a) * 255.0F));
}

// Border colour for the placeholder — dark grey at full alpha so the
// outline reads on any tint. The diagonals match.
constexpr ImU32 kBorderColour = IM_COL32(40, 40, 40, 220);

}  // namespace

void image_overlay(const std::vector<noted::domain::tool::ImagePrimitive>& images,
                   const ImageCanvasToScreenFn& canvas_to_screen,
                   const ImageTextureLookupFn& texture_lookup,
                   bool enabled) {
    if (!enabled || !canvas_to_screen) {
        return;
    }
    auto* dl = ImGui::GetBackgroundDrawList();
    if (dl == nullptr) {
        return;
    }
    for (const auto& im : images) {
        if (im.is_degenerate()) {
            continue;
        }
        const auto [sx0, sy0] = canvas_to_screen(im.x, im.y);
        const auto [sx1, sy1] = canvas_to_screen(im.x + static_cast<double>(im.width_px),
                                                 im.y + static_cast<double>(im.height_px));

        const ImVec2 p0{sx0, sy0};
        const ImVec2 p1{sx1, sy1};
        const ImU32 tint = pack_color(im.r, im.g, im.b, im.a);

        // Textured path (B.7.b.2b) — only if the asset is bound to a
        // GPU texture. Falls through to the placeholder block below
        // when the lookup returns 0 (no callback, no asset, decode in
        // flight, or asset has no source_bytes yet).
        const std::uint64_t tex_handle =
            (texture_lookup && im.asset_id != noted::domain::invalid_asset_id)
                ? texture_lookup(im.asset_id)
                : 0U;
        if (tex_handle != 0U) {
            // ImTextureID is ImU64 in the docking branch we ship —
            // a static_cast is the correct conversion (reinterpret
            // is for pointer types).
            dl->AddImage(static_cast<ImTextureID>(tex_handle),
                         p0,
                         p1,
                         /*uv_min=*/ImVec2{0.0F, 0.0F},
                         /*uv_max=*/ImVec2{1.0F, 1.0F},
                         tint);
            // A subtle 1 px border keeps the image readable against
            // any page background without competing with content.
            dl->AddRect(p0, p1, kBorderColour, /*rounding=*/0.0F, /*flags=*/0, /*thickness=*/1.0F);
            continue;
        }

        // Placeholder path — same triad as before B.7.b.2b.

        // 1. Tinted fill — the placeholder body.
        dl->AddRectFilled(p0, p1, tint);

        // 2. Border — 2 px dark frame.
        dl->AddRect(p0, p1, kBorderColour, /*rounding=*/0.0F, /*flags=*/0, /*thickness=*/2.0F);

        // 3. Diagonals — the classic "image not loaded" cross. Two
        //    lines from corner to corner make the placeholder
        //    unmistakable, even when the tint matches the page.
        dl->AddLine(p0, p1, kBorderColour, 1.5F);
        dl->AddLine(ImVec2{p0.x, p1.y}, ImVec2{p1.x, p0.y}, kBorderColour, 1.5F);

        // 4. Centred "Image" label. ImGui's CalcTextSize uses the
        //    current font; place it so the text centre aligns with
        //    the rectangle centre, but skip if the placeholder is
        //    smaller than the label (avoid clutter at tiny sizes).
        const auto* label = "Image";
        const auto label_size = ImGui::CalcTextSize(label);
        if (label_size.x + 8.0F < p1.x - p0.x && label_size.y + 8.0F < p1.y - p0.y) {
            const ImVec2 centre{(p0.x + p1.x) * 0.5F - label_size.x * 0.5F,
                                (p0.y + p1.y) * 0.5F - label_size.y * 0.5F};
            dl->AddText(centre, kBorderColour, label);
        }
    }
}

}  // namespace noted::ui::widget
