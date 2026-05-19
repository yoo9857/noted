#include "noted/ui/theme/theme.hpp"

#include <imgui.h>

namespace noted::ui::theme {

namespace {

// Shared layout sizing. Both themes use the same paddings + rounding
// so the only visual change is colour; users switching themes are
// not also re-learning button shapes.
void apply_sizing(ImGuiStyle& s) {
    s.WindowPadding = ImVec2(10.0F, 10.0F);
    s.FramePadding = ImVec2(8.0F, 4.0F);
    s.CellPadding = ImVec2(6.0F, 3.0F);
    s.ItemSpacing = ImVec2(8.0F, 4.0F);
    s.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    s.IndentSpacing = 18.0F;
    s.ScrollbarSize = 14.0F;
    s.GrabMinSize = 12.0F;

    s.WindowBorderSize = 1.0F;
    s.ChildBorderSize = 1.0F;
    s.PopupBorderSize = 1.0F;
    s.FrameBorderSize = 0.0F;
    s.TabBorderSize = 0.0F;

    s.WindowRounding = 6.0F;
    s.ChildRounding = 4.0F;
    s.FrameRounding = 4.0F;
    s.PopupRounding = 4.0F;
    s.ScrollbarRounding = 6.0F;
    s.GrabRounding = 4.0F;
    s.TabRounding = 4.0F;

    s.WindowTitleAlign = ImVec2(0.0F, 0.5F);
    s.WindowMenuButtonPosition = ImGuiDir_None;  // hide the default collapse arrow
    s.SeparatorTextAlign = ImVec2(0.0F, 0.5F);
    s.SeparatorTextPadding = ImVec2(20.0F, 3.0F);
}

// Dark palette. Neutral grays trending blue-cool so the warm tones in
// typical paintings/photos read correctly against the chrome. Single
// accent (#5294e2) used for selected / active / check states.
void apply_dark_palette(ImVec4* c) {
    c[ImGuiCol_Text] = ImVec4(0.890F, 0.898F, 0.910F, 1.000F);
    c[ImGuiCol_TextDisabled] = ImVec4(0.600F, 0.612F, 0.631F, 1.000F);

    c[ImGuiCol_WindowBg] = ImVec4(0.118F, 0.122F, 0.133F, 1.000F);
    c[ImGuiCol_ChildBg] = ImVec4(0.118F, 0.122F, 0.133F, 0.000F);
    c[ImGuiCol_PopupBg] = ImVec4(0.169F, 0.176F, 0.192F, 0.980F);

    c[ImGuiCol_Border] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_BorderShadow] = ImVec4(0.000F, 0.000F, 0.000F, 0.000F);

    c[ImGuiCol_FrameBg] = ImVec4(0.192F, 0.200F, 0.219F, 1.000F);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.227F, 0.239F, 0.267F, 1.000F);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.306F, 0.322F, 0.345F, 1.000F);

    c[ImGuiCol_TitleBg] = ImVec4(0.118F, 0.122F, 0.133F, 1.000F);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.169F, 0.176F, 0.192F, 1.000F);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.118F, 0.122F, 0.133F, 0.750F);

    c[ImGuiCol_MenuBarBg] = ImVec4(0.169F, 0.176F, 0.192F, 1.000F);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.118F, 0.122F, 0.133F, 0.530F);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.306F, 0.322F, 0.345F, 1.000F);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.380F, 0.404F, 0.439F, 1.000F);

    c[ImGuiCol_CheckMark] = ImVec4(0.322F, 0.580F, 0.886F, 1.000F);
    c[ImGuiCol_SliderGrab] = ImVec4(0.322F, 0.580F, 0.886F, 1.000F);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.412F, 0.667F, 0.945F, 1.000F);

    c[ImGuiCol_Button] = ImVec4(0.192F, 0.200F, 0.219F, 1.000F);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_ButtonActive] = ImVec4(0.322F, 0.580F, 0.886F, 1.000F);

    c[ImGuiCol_Header] = ImVec4(0.192F, 0.200F, 0.219F, 1.000F);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_HeaderActive] = ImVec4(0.322F, 0.580F, 0.886F, 1.000F);

    c[ImGuiCol_Separator] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.322F, 0.580F, 0.886F, 0.780F);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.412F, 0.667F, 0.945F, 1.000F);

    c[ImGuiCol_ResizeGrip] = ImVec4(0.247F, 0.259F, 0.282F, 0.500F);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.322F, 0.580F, 0.886F, 0.670F);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.412F, 0.667F, 0.945F, 0.950F);

    c[ImGuiCol_Tab] = ImVec4(0.169F, 0.176F, 0.192F, 1.000F);
    c[ImGuiCol_TabHovered] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_TabActive] = ImVec4(0.192F, 0.200F, 0.219F, 1.000F);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.118F, 0.122F, 0.133F, 1.000F);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.169F, 0.176F, 0.192F, 1.000F);

    c[ImGuiCol_PlotLines] = ImVec4(0.612F, 0.612F, 0.612F, 1.000F);
    c[ImGuiCol_PlotLinesHovered] = ImVec4(1.000F, 0.431F, 0.349F, 1.000F);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.322F, 0.580F, 0.886F, 1.000F);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.412F, 0.667F, 0.945F, 1.000F);

    c[ImGuiCol_TableHeaderBg] = ImVec4(0.169F, 0.176F, 0.192F, 1.000F);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.247F, 0.259F, 0.282F, 1.000F);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.192F, 0.200F, 0.219F, 1.000F);
    c[ImGuiCol_TableRowBg] = ImVec4(0.000F, 0.000F, 0.000F, 0.000F);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.000F, 1.000F, 1.000F, 0.040F);

    c[ImGuiCol_TextSelectedBg] = ImVec4(0.322F, 0.580F, 0.886F, 0.470F);
    c[ImGuiCol_DragDropTarget] = ImVec4(0.412F, 0.667F, 0.945F, 0.900F);

    c[ImGuiCol_NavHighlight] = ImVec4(0.412F, 0.667F, 0.945F, 1.000F);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000F, 1.000F, 1.000F, 0.700F);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800F, 0.800F, 0.800F, 0.200F);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000F, 0.000F, 0.000F, 0.600F);
}

// Light palette. Inverts the lightness curve but reuses the same
// accent so cross-theme selection state reads consistently. Slight
// cool tint on backgrounds prevents the warm-paper effect Adobe got
// criticized for in early Creative Cloud.
void apply_light_palette(ImVec4* c) {
    c[ImGuiCol_Text] = ImVec4(0.122F, 0.125F, 0.141F, 1.000F);
    c[ImGuiCol_TextDisabled] = ImVec4(0.431F, 0.439F, 0.467F, 1.000F);

    c[ImGuiCol_WindowBg] = ImVec4(0.961F, 0.961F, 0.969F, 1.000F);
    c[ImGuiCol_ChildBg] = ImVec4(0.961F, 0.961F, 0.969F, 0.000F);
    c[ImGuiCol_PopupBg] = ImVec4(1.000F, 1.000F, 1.000F, 0.980F);

    c[ImGuiCol_Border] = ImVec4(0.816F, 0.824F, 0.839F, 1.000F);
    c[ImGuiCol_BorderShadow] = ImVec4(0.000F, 0.000F, 0.000F, 0.000F);

    c[ImGuiCol_FrameBg] = ImVec4(1.000F, 1.000F, 1.000F, 1.000F);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.816F, 0.839F, 0.878F, 1.000F);

    c[ImGuiCol_TitleBg] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.816F, 0.824F, 0.839F, 1.000F);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.910F, 0.918F, 0.929F, 0.750F);

    c[ImGuiCol_MenuBarBg] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.961F, 0.961F, 0.969F, 0.530F);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.769F, 0.776F, 0.792F, 1.000F);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.659F, 0.667F, 0.682F, 1.000F);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.494F, 0.502F, 0.518F, 1.000F);

    c[ImGuiCol_CheckMark] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);
    c[ImGuiCol_SliderGrab] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.122F, 0.349F, 0.769F, 1.000F);

    c[ImGuiCol_Button] = ImVec4(1.000F, 1.000F, 1.000F, 1.000F);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_ButtonActive] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);

    c[ImGuiCol_Header] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.816F, 0.839F, 0.878F, 1.000F);
    c[ImGuiCol_HeaderActive] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);

    c[ImGuiCol_Separator] = ImVec4(0.816F, 0.824F, 0.839F, 1.000F);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.173F, 0.424F, 0.859F, 0.780F);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.122F, 0.349F, 0.769F, 1.000F);

    c[ImGuiCol_ResizeGrip] = ImVec4(0.769F, 0.776F, 0.792F, 0.500F);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.173F, 0.424F, 0.859F, 0.670F);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.122F, 0.349F, 0.769F, 0.950F);

    c[ImGuiCol_Tab] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_TabHovered] = ImVec4(0.816F, 0.839F, 0.878F, 1.000F);
    c[ImGuiCol_TabActive] = ImVec4(1.000F, 1.000F, 1.000F, 1.000F);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.961F, 0.961F, 0.969F, 1.000F);

    c[ImGuiCol_PlotLines] = ImVec4(0.388F, 0.388F, 0.388F, 1.000F);
    c[ImGuiCol_PlotLinesHovered] = ImVec4(1.000F, 0.431F, 0.349F, 1.000F);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.122F, 0.349F, 0.769F, 1.000F);

    c[ImGuiCol_TableHeaderBg] = ImVec4(0.910F, 0.918F, 0.929F, 1.000F);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.769F, 0.776F, 0.792F, 1.000F);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.857F, 0.863F, 0.878F, 1.000F);
    c[ImGuiCol_TableRowBg] = ImVec4(0.000F, 0.000F, 0.000F, 0.000F);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.000F, 0.000F, 0.000F, 0.040F);

    c[ImGuiCol_TextSelectedBg] = ImVec4(0.173F, 0.424F, 0.859F, 0.350F);
    c[ImGuiCol_DragDropTarget] = ImVec4(0.122F, 0.349F, 0.769F, 0.900F);

    c[ImGuiCol_NavHighlight] = ImVec4(0.173F, 0.424F, 0.859F, 1.000F);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(0.000F, 0.000F, 0.000F, 0.700F);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.200F, 0.200F, 0.200F, 0.200F);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.200F, 0.200F, 0.200F, 0.350F);
}

}  // namespace

void apply(ThemeKind kind) {
    ImGuiStyle& style = ImGui::GetStyle();
    apply_sizing(style);
    if (kind == ThemeKind::light) {
        apply_light_palette(style.Colors);
    } else {
        apply_dark_palette(style.Colors);
    }
}

auto label(ThemeKind kind) noexcept -> const char* {
    switch (kind) {
        case ThemeKind::dark:
            return "Dark";
        case ThemeKind::light:
            return "Light";
    }
    return "?";
}

}  // namespace noted::ui::theme
