#pragma once

// ImageToolHandler — input behaviour for the Image tool (Phase B.7).
//
// Click-to-place: on press, build an `ImagePrimitive` from the
// current `ImageOptions` snapshot and emit an `AddImageCommand`
// through the configured command sink. App wires the sink to
// `DocumentSession::execute` so the addition lands in the undo
// stack + .noted v5 round-trip.

#include <functional>
#include <memory>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/tool/image_input.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class ImageToolHandler final : public ToolInputHandler {
public:
    using CommandSink = std::function<void(std::unique_ptr<noted::domain::Command>)>;

    ImageToolHandler(CommandSink sink, const noted::domain::tool::ToolState& tools) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

private:
    CommandSink sink_;
    const noted::domain::tool::ToolState& tools_;
};

}  // namespace noted::app::input
