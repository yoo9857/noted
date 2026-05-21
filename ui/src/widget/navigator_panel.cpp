#include "noted/ui/widget/navigator_panel.hpp"

#include <algorithm>

#include <imgui.h>

#include "noted/engine/canvas/page.hpp"

namespace noted::ui::widget {

namespace {

constexpr float kThumbMinW = 200.0F;
constexpr float kThumbMinH = 130.0F;

constexpr ImU32 kCanvasFill = IM_COL32(0x18, 0x18, 0x1B, 0xFF);
constexpr ImU32 kCanvasBorder = IM_COL32(0x40, 0x40, 0x45, 0xFF);
constexpr ImU32 kPageFill = IM_COL32(0xE5, 0xE5, 0xEA, 0xFF);
constexpr ImU32 kPageBorder = IM_COL32(0x90, 0x90, 0x95, 0xFF);
constexpr ImU32 kViewportStroke = IM_COL32(0xFF, 0x4A, 0x4A, 0xC0);
constexpr ImU32 kViewportFill = IM_COL32(0xFF, 0x4A, 0x4A, 0x18);

}  // namespace

auto navigator_panel(const NavigatorInputs& in,
                     std::span<const noted::canvas::Page> pages,
                     float top_offset_px) -> NavigatorResult {
    NavigatorResult out{};
    if (in.canvas_w == 0 || in.canvas_h == 0) {
        return out;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kRightGutter = 12.0F;
    constexpr float kPanelW = 260.0F;
    const float panel_x = vp->Size.x - kPanelW - kRightGutter;

    ImGui::SetNextWindowPos(ImVec2(panel_x, top_offset_px), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kPanelW, 0.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Navigator")) {
        ImGui::End();
        return out;
    }

    // Panel-level thumb area is a fixed-shape dark box; inside it the
    // canvas itself is drawn at a SINGLE uniform scale and centred,
    // so pages + viewport keep their real aspect ratio regardless of
    // the panel's resize state.
    const float content_w = std::max(kThumbMinW, ImGui::GetContentRegionAvail().x);
    const float thumb_w = content_w;
    const float thumb_h = std::max(kThumbMinH, thumb_w * 0.66F);

    const ImVec2 thumb_min = ImGui::GetCursorScreenPos();
    const ImVec2 thumb_max(thumb_min.x + thumb_w, thumb_min.y + thumb_h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(thumb_min, thumb_max, kCanvasFill, 4.0F);
    dl->AddRect(thumb_min, thumb_max, kCanvasBorder, 4.0F, 0, 1.0F);

    // Uniform scale + centring. `s` is canvas-pixels → thumb-pixels.
    const float s = std::min(thumb_w / static_cast<float>(in.canvas_w),
                             thumb_h / static_cast<float>(in.canvas_h));
    const float canvas_draw_w = static_cast<float>(in.canvas_w) * s;
    const float canvas_draw_h = static_cast<float>(in.canvas_h) * s;
    const ImVec2 canvas_min(thumb_min.x + (thumb_w - canvas_draw_w) * 0.5F,
                            thumb_min.y + (thumb_h - canvas_draw_h) * 0.5F);
    const ImVec2 canvas_max(canvas_min.x + canvas_draw_w, canvas_min.y + canvas_draw_h);

    // The canvas frame and the page outlines used to draw with the
    // same grey, producing a visible "ghost rectangle" wherever the
    // two overlapped — page borders overshooting the canvas outline,
    // canvas outline showing through the centred padding bands. The
    // fix is simple: no separate canvas outline (the dark thumb +
    // bright page fills give enough boundary) and no page borders
    // (the fill colour stands out cleanly on its own).
    for (const auto& p : pages) {
        const ImVec2 p_min(canvas_min.x + p.origin_x_px * s, canvas_min.y + p.origin_y_px * s);
        const ImVec2 p_max(p_min.x + p.extent_w_px * s, p_min.y + p.extent_h_px * s);
        dl->AddRectFilled(p_min, p_max, kPageFill);
    }

    // Viewport rectangle. The visible canvas region is:
    //   [(-tx) / scale, (window_w - tx) / scale] in canvas-x, same on y.
    if (in.scale > 0.0 && in.window_w > 0 && in.window_h > 0) {
        const double vx0 = -in.translation_x / in.scale;
        const double vy0 = -in.translation_y / in.scale;
        const double vx1 = (static_cast<double>(in.window_w) - in.translation_x) / in.scale;
        const double vy1 = (static_cast<double>(in.window_h) - in.translation_y) / in.scale;
        const ImVec2 v_min(canvas_min.x + static_cast<float>(vx0) * s,
                           canvas_min.y + static_cast<float>(vy0) * s);
        const ImVec2 v_max(canvas_min.x + static_cast<float>(vx1) * s,
                           canvas_min.y + static_cast<float>(vy1) * s);
        const ImVec2 c_min(std::max(canvas_min.x, v_min.x), std::max(canvas_min.y, v_min.y));
        const ImVec2 c_max(std::min(canvas_max.x, v_max.x), std::min(canvas_max.y, v_max.y));
        if (c_max.x > c_min.x && c_max.y > c_min.y) {
            dl->AddRectFilled(c_min, c_max, kViewportFill, 2.0F);
            dl->AddRect(c_min, c_max, kViewportStroke, 2.0F, 0, 1.5F);
        }
    }

    // Click / drag — translate via the centred canvas rect (clicks in
    // the padding bands are ignored).
    ImGui::InvisibleButton("##nav_hit", ImVec2(thumb_w, thumb_h));
    const bool active =
        ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if ((active || clicked) && canvas_draw_w > 0.0F && canvas_draw_h > 0.0F) {
        const auto mouse = ImGui::GetIO().MousePos;
        const float u = (mouse.x - canvas_min.x) / canvas_draw_w;
        const float v = (mouse.y - canvas_min.y) / canvas_draw_h;
        if (u >= 0.0F && u <= 1.0F && v >= 0.0F && v <= 1.0F) {
            out.pan_to_canvas_point = std::make_pair(static_cast<double>(u) * in.canvas_w,
                                                     static_cast<double>(v) * in.canvas_h);
        }
    }

    ImGui::End();
    return out;
}

}  // namespace noted::ui::widget
