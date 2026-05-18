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
        ImGui::SameLine(0.0F, 24.0F);
        ImGui::TextDisabled("noted v0.x");
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}  // namespace noted::ui::widget
