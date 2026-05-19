#include "ui/dirty_prompt.hpp"

#include <imgui.h>

namespace noted::app {

namespace {
constexpr const char* kPopupTitle = "Unsaved changes##dirty";
}  // namespace

void DirtyPrompt::arm(PendingAction action) noexcept {
    if (pending_ == PendingAction::none) {
        pending_ = action;
        should_open_ = true;
    }
}

void DirtyPrompt::draw(const std::function<bool()>& save_cb,
                       const std::function<void(PendingAction)>& execute_cb) {
    // Drain the queued OpenPopup. ImGui requires the call from
    // inside a frame, so the arming sites only set a flag and the
    // actual OpenPopup happens here. Reset before opening so the
    // same flag can be re-armed within the same frame without a
    // double-open (BeginPopupModal silently absorbs a second open
    // on an already-open popup, but the explicit reset keeps the
    // state machine readable).
    if (should_open_) {
        should_open_ = false;
        ImGui::OpenPopup(kPopupTitle);
    }

    // Center the modal on the main viewport. SetNextWindowPos only
    // takes effect on Appearing — re-runs pick up wherever the user
    // last dragged it, matching the standard ImGui pattern.
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal(kPopupTitle, nullptr, kFlags)) {
        return;
    }

    ImGui::TextUnformatted("You have unsaved changes. Save before continuing?");
    ImGui::Spacing();
    constexpr ImVec2 kButton{110.0F, 0.0F};

    // Snapshot the action before any branch — the callbacks may
    // re-arm (e.g. a future "save and quit" shortcut) and we want
    // the frozen value to flow through.
    const PendingAction action_to_run = pending_;

    if (ImGui::Button("Save", kButton)) {
        if (save_cb && save_cb()) {
            ImGui::CloseCurrentPopup();
            pending_ = PendingAction::none;
            if (action_to_run == PendingAction::quit) {
                confirmed_exit_ = true;
            }
            if (execute_cb) {
                execute_cb(action_to_run);
            }
        }
        // On save failure / user-cancelled Save As: stay in the
        // modal so the user can pick Discard or Cancel instead.
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", kButton)) {
        ImGui::CloseCurrentPopup();
        pending_ = PendingAction::none;
        if (action_to_run == PendingAction::quit) {
            confirmed_exit_ = true;
        }
        if (execute_cb) {
            execute_cb(action_to_run);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", kButton)) {
        pending_ = PendingAction::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace noted::app
