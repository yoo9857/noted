#pragma once

// Save-changes prompt — the modal that intercepts closing the window
// or starting a new document while the current one has unsaved edits.
//
// State machine (full lifecycle):
//
//   arm(action)
//     ├─ pending_action_ ← action
//     └─ should_open_    ← true
//
//   draw()  [next ImGui frame]
//     ├─ drain should_open_ → ImGui::OpenPopup
//     └─ BeginPopupModal
//         ├─ Save  → save_cb(); on true → execute_cb(action) →
//         │         confirmed_exit_ flips if action==quit, reset
//         ├─ Discard → execute_cb(action) → reset
//         └─ Cancel → reset (pending_action_ ← none)
//
// `confirmed_exit_` is the flag the host's loop-top close intercept
// checks. Once true, the loop breaks regardless of dirty state —
// the user has already chosen.
//
// Pure UI/state — no GLFW, no engine, no domain. The host supplies
// the "actually save" and "actually execute" callbacks because both
// reach into systems the prompt has no business knowing about
// (file pickers, the GLFW window handle, document reset).

#include <functional>

namespace noted::app {

class DirtyPrompt {
public:
    enum class PendingAction : int { none = 0, quit = 1, new_doc = 2 };

    DirtyPrompt() = default;
    DirtyPrompt(const DirtyPrompt&) = delete;
    auto operator=(const DirtyPrompt&) -> DirtyPrompt& = delete;
    DirtyPrompt(DirtyPrompt&&) noexcept = default;
    auto operator=(DirtyPrompt&&) noexcept -> DirtyPrompt& = default;
    ~DirtyPrompt() = default;

    // Queue the modal for the next ImGui frame. No-op if a different
    // action is already pending — first writer wins.
    void arm(PendingAction action) noexcept;

    [[nodiscard]] auto pending_action() const noexcept -> PendingAction { return pending_; }
    [[nodiscard]] auto has_confirmed_exit() const noexcept -> bool { return confirmed_exit_; }

    // Per-frame draw. No-op when pending_action() is `none`.
    //
    // `save_cb`    - called when the user clicks Save. Returns true
    //                if the write actually completed (modal closes
    //                + execute_cb fires); false leaves the modal up.
    // `execute_cb` - called on Save success or Discard with the
    //                frozen action. Host should perform the action
    //                (close the window for quit; reset the doc for
    //                new_doc). When action == quit, the prompt
    //                also flips `confirmed_exit_` so the host's
    //                loop-top intercept lets the exit proceed.
    void draw(const std::function<bool()>& save_cb,
              const std::function<void(PendingAction)>& execute_cb);

private:
    PendingAction pending_{PendingAction::none};
    bool should_open_{false};
    bool confirmed_exit_{false};
};

}  // namespace noted::app
