#pragma once

// TextToolHandler — input behaviour for the Text tool (Phase B.6).
//
// Unlike the drag-based tools, Text uses **click + type**: on press
// the handler starts an "editing session" at the canvas point;
// keystrokes are collected by an `ImGui::InputText` widget that the
// `text_overlay` renders each frame; Enter (or focus loss) commits
// the buffer into a `TextPrimitive`. The handler exposes a mutable
// `editing()` accessor so the overlay can write directly into the
// in-flight buffer + position the InputText.
//
// **Click-while-editing** semantics: if the user clicks a new
// position while already editing, the in-flight buffer is
// committed (if non-empty) and a new editing session starts at the
// new position. Same UX every paint app uses — no "lost text"
// surprise.
//
// `on_moved` and `on_released` are no-ops — text editing isn't a
// drag.
//
// `on_deactivated` commits the current editing session (if any) so
// switching tools doesn't lose typed text. Pure-empty buffers are
// dropped silently.

#include <optional>
#include <string>
#include <vector>

#include "noted/domain/tool/text_input.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class TextToolHandler final : public ToolInputHandler {
public:
    TextToolHandler(std::vector<noted::domain::tool::TextPrimitive>& texts,
                    const noted::domain::tool::ToolState& tools) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    // Editing session state. `nullopt` between clicks. The overlay
    // widget grabs a mutable reference each frame to render the
    // InputText bound to `buffer`. The state struct itself lives in
    // `domain::tool` so `ui` can read it without depending on `app`.
    using EditingState = noted::domain::tool::TextEditingState;

    [[nodiscard]] auto editing() noexcept -> std::optional<EditingState>& { return editing_; }
    [[nodiscard]] auto editing() const noexcept -> const std::optional<EditingState>& {
        return editing_;
    }

    // Push the in-flight buffer into the committed-texts vector if
    // it's non-empty (post-trim), then clear the editing session.
    // Called by the overlay on Enter or focus-loss, and by
    // `on_deactivated` when the user switches tools mid-typing.
    void commit_editing();

    // Drop the in-flight buffer without committing. Called by the
    // overlay on Esc.
    void cancel_editing() noexcept;

    [[nodiscard]] auto texts() const noexcept
        -> const std::vector<noted::domain::tool::TextPrimitive>& {
        return texts_;
    }

private:
    std::vector<noted::domain::tool::TextPrimitive>& texts_;
    const noted::domain::tool::ToolState& tools_;
    std::optional<noted::domain::tool::TextEditingState> editing_{};
};

}  // namespace noted::app::input
