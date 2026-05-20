#include "noted/ui/widget/page_strip.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "noted/engine/canvas/page.hpp"

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto background_glyph(noted::canvas::PageBackground bg) -> const char* {
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
        std::snprintf(label.data(),
                      label.size(),
                      "Page %zu  (%s)",
                      i + 1U,
                      background_glyph(page.background));
        if (ImGui::Selectable(label.data(), false)) {
            out.focus_request = i;
        }

        // Thumbnail placeholder — a flat coloured rect so the user can
        // see "this row corresponds to a real page" even before the
        // future GPU thumbnail render lands. Width tracks the
        // available rail width; height is a fixed aspect-ish chunk.
        const auto avail = ImGui::GetContentRegionAvail().x;
        const float thumb_h = 36.0F;
        const auto cursor = ImGui::GetCursorScreenPos();
        const ImVec2 tl{cursor.x, cursor.y};
        const ImVec2 br{cursor.x + avail, cursor.y + thumb_h};
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(tl, br, IM_COL32(220, 220, 220, 255));
        draw->AddRect(tl, br, IM_COL32(110, 110, 110, 255));
        // Reserve the rect so subsequent widgets flow below it.
        ImGui::Dummy(ImVec2{avail, thumb_h});

        if (ImGui::BeginPopupContextItem("page_row_ctx")) {
            if (ImGui::MenuItem("Remove")) {
                out.remove_request = i;
            }
            ImGui::EndPopup();
        }

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
