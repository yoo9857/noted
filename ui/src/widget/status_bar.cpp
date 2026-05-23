#include "noted/ui/widget/status_bar.hpp"

#include <imgui.h>

namespace noted::ui::widget {

void status_bar(const StatusBarInfo& info) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }
    constexpr float kHeight = 22.0F;
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - kHeight});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, kHeight});

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

    // Pin the background to a neutral tone so the bar reads as
    // chrome rather than a regular window. ImGui's default window
    // bg blends into the canvas at higher opacity.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08F, 0.08F, 0.10F, 0.95F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 2.0F));

    if (ImGui::Begin("##status_bar", nullptr, kFlags)) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("frame %llu", static_cast<unsigned long long>(info.frame_index));
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0F / io.Framerate);
        if (info.zoom_pct > 0.0F) {
            ImGui::SameLine(0.0F, 24.0F);
            ImGui::Text("%d%%", static_cast<int>(info.zoom_pct + 0.5F));
        }
        // Active-layer readout. Photoshop / Procreate keep "Painting on:
        // <name>" visible at the bottom because the alternative is
        // silently painting into the wrong layer (or a hidden one) and
        // wondering why nothing shows up. The warning tag flips to
        // bright orange when paint will silently no-op.
        if (!info.active_layer_name.empty()) {
            ImGui::SameLine(0.0F, 24.0F);
            ImGui::TextDisabled("Painting:");
            ImGui::SameLine(0.0F, 4.0F);
            ImGui::TextUnformatted(info.active_layer_name.data(),
                                   info.active_layer_name.data() + info.active_layer_name.size());
            const bool blocked = info.active_layer_orphaned || !info.active_layer_visible ||
                                 info.active_layer_locked;
            if (blocked) {
                const char* tag = info.active_layer_orphaned   ? "[no active]"
                                  : !info.active_layer_visible ? "[hidden]"
                                                               : "[locked]";
                ImGui::SameLine(0.0F, 6.0F);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.65F, 0.20F, 1.0F));
                ImGui::TextUnformatted(tag);
                ImGui::PopStyleColor();
            }
        }
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::TextDisabled("noted v0.x");
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}  // namespace noted::ui::widget
