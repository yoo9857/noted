#pragma once

// PageRenderer — draws every Page in a PageList as a paper-coloured
// rectangle (with an optional pattern) into the canvas. One draw call
// per page; the caller layers the compositor + stroke engine on top.
//
// Phase A.3.b — pairs with the pure-logic Page + PageList from
// Phase A.3.a (PR #63) and the page_bg.slang shader the renderer
// pipes draws through.
//
// Pipeline shape:
//   - No vertex buffer (vertex shader builds a 6-vertex quad from
//     SV_VertexID).
//   - One 32-byte vertex-stage push constant per page carrying
//     canvas size + page origin + extent + background ordinal.
//   - Triangle list, no culling (renderer's standard convention),
//     no blend — the page background is the canvas's base colour
//     layer.
//
// The renderer owns no PageList state — the caller (App) owns the
// list and passes it into `render` each frame. PageRenderer is a
// pure GPU primitive.

#include <cstdint>
#include <optional>

#include <vulkan/vulkan.h>

#include "noted/engine/canvas/page.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"

namespace noted::gpu {
class Device;
class ShaderModule;
}  // namespace noted::gpu

namespace noted::canvas {

class PageRenderer {
public:
    struct CreateInfo {
        const noted::gpu::Device* device{nullptr};
        const noted::gpu::ShaderModule* vs_module{nullptr};
        const noted::gpu::ShaderModule* ps_module{nullptr};
        // Format of the canvas attachment the renderer will draw
        // into. Must match `CanvasRenderTarget::format()`.
        VkFormat canvas_format{VK_FORMAT_R8G8B8A8_UNORM};
    };

    [[nodiscard]] static auto create(const CreateInfo& info) -> Result<PageRenderer>;

    PageRenderer(PageRenderer&&) noexcept = default;
    auto operator=(PageRenderer&&) noexcept -> PageRenderer& = default;
    PageRenderer(const PageRenderer&) = delete;
    auto operator=(const PageRenderer&) -> PageRenderer& = delete;
    ~PageRenderer() = default;

    // Issue one `vkCmdDraw(6, 1, 0, 0)` per page in `list`. Caller
    // is responsible for an active `vkCmdBeginRendering` whose
    // color attachment is the canvas at COLOR_ATTACHMENT_OPTIMAL.
    // Empty list is a no-op.
    void render(VkCommandBuffer cb, VkExtent2D canvas_extent, const PageList& list) noexcept;

private:
    PageRenderer() = default;

    std::optional<noted::gpu::PipelineLayout> layout_;
    std::optional<noted::gpu::GraphicsPipeline> pipeline_;
};

}  // namespace noted::canvas
