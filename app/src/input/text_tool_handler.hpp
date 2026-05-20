#pragma once

// TextToolHandler — input behaviour for the Text tool (Phase B.6).
//
// Click-to-type: on press the handler starts an "editing session" at
// the canvas point; keystrokes are collected by an
// `ImGui::InputText` widget the `text_overlay` renders each frame;
// Enter (or focus loss) commits the buffer through the configured
// command sink, which the App wires to `DocumentSession::execute`.
// Each commit emits an `AddTextCommand` so undo + .noted round-trip
// work.
//
// Click-while-editing semantics: clicking a new position while
// already editing commits the in-flight buffer (if non-empty) and
// starts a new editing session at the new position. No lost text.
//
// `on_deactivated` commits the current session so switching tools
// doesn't drop typed text. Empty buffers are silently discarded.

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/tool/text_input.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class TextToolHandler final : public ToolInputHandler {
public:
    using CommandSink = std::function<void(std::unique_ptr<noted::domain::Command>)>;

    TextToolHandler(CommandSink sink, const noted::domain::tool::ToolState& tools) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    // Editing session state lives in `domain::tool::TextEditingState`
    // (Phase B.6) so the ui-layer overlay can read/write the buffer
    // without depending on `app/`.
    using EditingState = noted::domain::tool::TextEditingState;

    [[nodiscard]] auto editing() noexcept -> std::optional<EditingState>& { return editing_; }
    [[nodiscard]] auto editing() const noexcept -> const std::optional<EditingState>& {
        return editing_;
    }

    // Push the in-flight buffer through the command sink as an
    // `AddTextCommand` if non-empty (post-trim), then clear the
    // editing session.
    void commit_editing();

    // Drop the in-flight buffer without committing.
    void cancel_editing() noexcept;

private:
    CommandSink sink_;
    const noted::domain::tool::ToolState& tools_;
    std::optional<noted::domain::tool::TextEditingState> editing_{};
};

}  // namespace noted::app::input
