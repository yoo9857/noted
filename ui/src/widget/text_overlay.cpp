#include "noted/ui/widget/text_overlay.hpp"

#include <algorithm>

#include <imgui.h>

#include "noted/domain/tool/text_input.hpp"

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto pack_color(float r, float g, float b, float a) noexcept -> ImU32 {
    const auto clamp01 = [](float v) -> float { return std::clamp(v, 0.0F, 1.0F); };
    return IM_COL32(static_cast<int>(clamp01(r) * 255.0F),
                    static_cast<int>(clamp01(g) * 255.0F),
                    static_cast<int>(clamp01(b) * 255.0F),
                    static_cast<int>(clamp01(a) * 255.0F));
}

void draw_committed_texts(const std::vector<noted::domain::tool::TextPrimitive>& texts,
                          const TextCanvasToScreenFn& canvas_to_screen,
                          bool enabled) {
    if (!enabled) {
        return;
    }
    auto* dl = ImGui::GetBackgroundDrawList();
    if (dl == nullptr) {
        return;
    }
    // The default font is the CJK-aware font ImGuiHost loaded at
    // startup (`probe_cjk_font` walks malgun.ttf / Apple SD / Noto
    // CJK / Nanum). AddText's per-call font_size override lets the
    // user pick any size at runtime without recompiling the atlas.
    auto* font = ImGui::GetFont();
    for (const auto& t : texts) {
        if (t.is_empty()) {
            continue;
        }
        const auto [sx, sy] = canvas_to_screen(t.x, t.y);
        const ImU32 colour = pack_color(t.r, t.g, t.b, t.a);
        dl->AddText(font, t.font_size_px, ImVec2{sx, sy}, colour, t.content.c_str());
    }
}

void draw_editing_input(std::optional<noted::domain::tool::TextEditingState>& editing_opt,
                        const TextCanvasToScreenFn& canvas_to_screen,
                        const std::function<void()>& on_commit,
                        const std::function<void()>& on_cancel) {
    if (!editing_opt.has_value()) {
        return;
    }
    auto& e = *editing_opt;
    const auto [sx, sy] = canvas_to_screen(e.position_x, e.position_y);

    // A borderless, transparent, auto-sized window hosts the
    // InputText. NoSavedSettings keeps the window invisible to
    // ImGui's .ini persistence — we don't want every keystroke
    // session to leak into the layout file.
    constexpr auto kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowPos(ImVec2{sx, sy});
    ImGui::SetNextWindowBgAlpha(0.0F);
    if (ImGui::Begin("##text_edit_overlay", nullptr, kFlags)) {
        if (e.needs_focus) {
            ImGui::SetKeyboardFocusHere();
            e.needs_focus = false;
        }
        // InputText renders at the default UI font size — this
        // ImGui version's `PushFont` doesn't take a runtime size
        // parameter. The committed text still draws at the chosen
        // size (see `draw_committed_texts`); the discrepancy lives
        // only during the in-flight edit and disappears on Enter.
        const bool enter = ImGui::InputText(
            "##text", e.buffer, sizeof(e.buffer), ImGuiInputTextFlags_EnterReturnsTrue);

        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat=*/false);

        if (enter && on_commit) {
            on_commit();
        } else if (escape && on_cancel) {
            on_cancel();
        }
    }
    ImGui::End();
}

}  // namespace

void text_overlay(const std::vector<noted::domain::tool::TextPrimitive>& texts,
                  std::optional<noted::domain::tool::TextEditingState>& editing,
                  const TextCanvasToScreenFn& canvas_to_screen,
                  const std::function<void()>& on_commit,
                  const std::function<void()>& on_cancel,
                  bool enabled) {
    if (!canvas_to_screen) {
        return;
    }
    draw_committed_texts(texts, canvas_to_screen, enabled);
    draw_editing_input(editing, canvas_to_screen, on_commit, on_cancel);
}

}  // namespace noted::ui::widget
