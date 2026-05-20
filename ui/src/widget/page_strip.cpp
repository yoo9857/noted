#include "noted/ui/widget/page_strip.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "noted/engine/canvas/page.hpp"

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto background_name(noted::canvas::PageBackground bg) -> const char* {
    using noted::canvas::PageBackground;
    switch (bg) {
        case PageBackground::blank:
            return "blank";
        case PageBackground::lined:
            return "lined";
        case PageBackground::grid:
            return "grid";
        case PageBackground::dotted:
            return "dots";
    }
    return "blank";
}

// Draw a small representation of the page's background pattern into
// the strip's thumbnail rect. Mirrors `page_bg.slang`'s patterns at
// thumbnail resolution so the user can tell pages apart at a glance
// without having to scroll the canvas to each one. Paper colour
// matches the shader's `kPaperColor`; line colour is a soft grey.
void draw_pattern_preview(ImDrawList* draw,
                          ImVec2 tl,
                          ImVec2 br,
                          noted::canvas::PageBackground bg) {
    using noted::canvas::PageBackground;
    constexpr ImU32 kPaper = IM_COL32(245, 243, 235, 255);
    constexpr ImU32 kLine = IM_COL32(160, 160, 160, 255);
    constexpr ImU32 kBorder = IM_COL32(90, 90, 90, 255);

    draw->AddRectFilled(tl, br, kPaper);

    const float w = br.x - tl.x;
    const float h = br.y - tl.y;

    switch (bg) {
        case PageBackground::blank:
            break;
        case PageBackground::lined: {
            constexpr int kLines = 3;
            for (int i = 1; i <= kLines; ++i) {
                const float y = tl.y + h * static_cast<float>(i) / static_cast<float>(kLines + 1);
                draw->AddLine({tl.x + 2.0F, y}, {br.x - 2.0F, y}, kLine, 1.0F);
            }
            break;
        }
        case PageBackground::grid: {
            constexpr int kCols = 6;
            constexpr int kRows = 3;
            for (int c = 1; c < kCols; ++c) {
                const float x = tl.x + w * static_cast<float>(c) / static_cast<float>(kCols);
                draw->AddLine({x, tl.y + 2.0F}, {x, br.y - 2.0F}, kLine, 1.0F);
            }
            for (int r = 1; r < kRows; ++r) {
                const float y = tl.y + h * static_cast<float>(r) / static_cast<float>(kRows);
                draw->AddLine({tl.x + 2.0F, y}, {br.x - 2.0F, y}, kLine, 1.0F);
            }
            break;
        }
        case PageBackground::dotted: {
            constexpr int kCols = 6;
            constexpr int kRows = 3;
            for (int r = 1; r < kRows; ++r) {
                for (int c = 1; c < kCols; ++c) {
                    const float x = tl.x + w * static_cast<float>(c) / static_cast<float>(kCols);
                    const float y = tl.y + h * static_cast<float>(r) / static_cast<float>(kRows);
                    draw->AddCircleFilled({x, y}, 1.2F, kLine);
                }
            }
            break;
        }
    }

    draw->AddRect(tl, br, kBorder);
}

}  // namespace

auto page_strip(const noted::canvas::PageList& pages, bool* open) -> PageStripResult {
    PageStripResult out{};
    if (open != nullptr && !*open) {
        return out;
    }

    ImGui::SetNextWindowSize(ImVec2{200.0F, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Pages", open)) {
        ImGui::End();
        return out;
    }

    const auto& list = pages.pages();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& page = list[i];
        ImGui::PushID(static_cast<int>(i));

        std::array<char, 64> label{};
        std::snprintf(
            label.data(), label.size(), "Page %zu  (%s)", i + 1U, background_name(page.background));
        if (ImGui::Selectable(label.data(), false)) {
            out.focus_request = i;
        }
        // Context menu attached to the label Selectable — right-click
        // anywhere on the row label opens it. Must come immediately
        // after the Selectable so BeginPopupContextItem binds to the
        // right item.
        if (ImGui::BeginPopupContextItem("page_row_ctx")) {
            if (ImGui::MenuItem("Remove")) {
                out.remove_request = i;
            }
            ImGui::EndPopup();
        }

        // Mini preview of the page's background pattern so the user
        // can tell pages apart at a glance without scrolling the
        // canvas. The Dummy reserves layout space for the manually-
        // drawn rect so the separator + next row land below it.
        const auto avail = ImGui::GetContentRegionAvail().x;
        constexpr float kThumbH = 36.0F;
        const auto cursor = ImGui::GetCursorScreenPos();
        draw_pattern_preview(ImGui::GetWindowDrawList(),
                             ImVec2{cursor.x, cursor.y},
                             ImVec2{cursor.x + avail, cursor.y + kThumbH},
                             page.background);
        ImGui::Dummy(ImVec2{avail, kThumbH});

        ImGui::Separator();
        ImGui::PopID();
    }

    if (ImGui::Button("+ Add page", ImVec2{-1.0F, 0.0F})) {
        out.add_request = true;
    }

    ImGui::End();
    return out;
}

auto camera_translation_y_for_page(const noted::canvas::PageList& pages,
                                   std::size_t index,
                                   double current_translation_y,
                                   double scale,
                                   double target_screen_y_px) noexcept -> double {
    if (index >= pages.size()) {
        return current_translation_y;
    }
    const double origin_y = static_cast<double>(pages.pages()[index].origin_y_px);
    return target_screen_y_px - origin_y * scale;
}

}  // namespace noted::ui::widget
