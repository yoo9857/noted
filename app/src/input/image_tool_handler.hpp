#pragma once

// ImageToolHandler — input behaviour for the Image tool (Phase B.7).
//
// Click-to-place: on press, commit a new `ImagePrimitive` at the
// canvas point using the current `ImageOptions` snapshot. No drag
// preview — the placeholder appears immediately on press. This is
// deliberate: real images (B.7.b) will likely keep the same press-
// commits-immediately semantics (the file picker fires before the
// press anchor), so the v0.x stub matches the eventual shape.
//
// `on_moved`, `on_released`, `on_deactivated` are no-ops — no in-
// flight state to clean up.

#include <vector>

#include "noted/domain/tool/image_input.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class ImageToolHandler final : public ToolInputHandler {
public:
    ImageToolHandler(std::vector<noted::domain::tool::ImagePrimitive>& images,
                     const noted::domain::tool::ToolState& tools) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    [[nodiscard]] auto images() const noexcept
        -> const std::vector<noted::domain::tool::ImagePrimitive>& {
        return images_;
    }

private:
    std::vector<noted::domain::tool::ImagePrimitive>& images_;
    const noted::domain::tool::ToolState& tools_;
};

}  // namespace noted::app::input
