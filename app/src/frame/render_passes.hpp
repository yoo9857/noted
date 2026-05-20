#pragma once

// RenderPasses — the four-pass canvas pipeline coordinator.
//
// Phase R.3 of the App-layer decomposition (per ADR 0032). Owns the
// per-frame draw orchestration: building clear values + pass
// descriptors + calling `Renderer::render_with_canvas` + dispatching
// each of the four pass record callbacks.
//
// The four passes (from ADR 0031 / Phase B.2):
//   1. canvas — `PageRenderer` + `LayerCompositor` into the canvas
//   2. strokes — `StrokeEngine` into the strokes target
//   3. overlay — composite strokes_target onto canvas (SRC_OVER)
//   4. swapchain — composite canvas onto the swapchain + ImGui
//
// **Non-owning.** Every GPU resource is owned by `App`; this class
// holds references and orchestrates. The `Deps` struct names every
// dependency at construction time so the contract is explicit and
// the class is self-documenting.
//
// Lifetime: built once, after all GPU resources have been
// initialized. Move/copy disabled because reference members make
// the class structurally non-movable AND the design intent is
// "constructed once at startup, lives for the App's lifetime".
//
// What is NOT in RenderPasses:
//   - Swapchain re-creation (`App::recreate_swapchain`) — that's
//     **owner work** (App owns the swapchain + canvas + strokes
//     target and must reallocate them). RenderPasses just consumes
//     the post-recreate resources.
//   - The descriptor re-binding after a target resize — that's
//     also App's concern; RenderPasses reads whatever the
//     descriptor sets currently point at.

#include <memory>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"

namespace noted::canvas {
class Camera;
class PageRenderer;
}  // namespace noted::canvas

namespace noted::compositor {
class LayerCompositor;
}  // namespace noted::compositor

namespace noted::gpu {
class CanvasRenderTarget;
class DescriptorSet;
class Device;
class GraphicsPipeline;
class PipelineLayout;
class Renderer;
class StrokeTarget;
class Swapchain;
}  // namespace noted::gpu

namespace noted::stroke {
class StrokeEngine;
}  // namespace noted::stroke

namespace noted::ui {
class ImGuiHost;
}  // namespace noted::ui

namespace noted::app {
class DocumentSession;
struct DemoScene;
}  // namespace noted::app

namespace noted::app::frame {

class RenderPasses {
public:
    // Every dependency named, every reference non-owning. The struct
    // is constructed inline at the App::create call site; collecting
    // 16 references into one self-documenting parameter keeps the
    // class's constructor signature manageable.
    struct Deps {
        // ---- Targets + renderer ------------------------------------------
        const noted::gpu::Device& device;
        const noted::gpu::Swapchain& swapchain;
        noted::gpu::Renderer& renderer;
        noted::gpu::CanvasRenderTarget& canvas;
        noted::gpu::StrokeTarget& strokes_target;

        // ---- Canvas pass (paper + layers) --------------------------------
        // `PageRenderer::render` is non-const (it binds pipelines /
        // pushes constants), so the reference must be mutable.
        // `LayerCompositor::composite` likewise.
        noted::canvas::PageRenderer& page_renderer;
        noted::app::DocumentSession& session;
        noted::compositor::LayerCompositor& layer_compositor;
        const noted::app::DemoScene& scene;

        // ---- Strokes pass ------------------------------------------------
        noted::stroke::StrokeEngine& stroke_engine;

        // ---- Composite pipelines (shared layout, two pipelines) ----------
        const noted::gpu::PipelineLayout& composite_pipeline_layout;
        const noted::gpu::GraphicsPipeline& composite_pipeline;
        const noted::gpu::GraphicsPipeline& composite_overlay_pipeline;

        // ---- Descriptor sets that the composite shader samples -----------
        const noted::gpu::DescriptorSet& canvas_set;
        const noted::gpu::DescriptorSet& strokes_set;

        // ---- Swapchain pass extras ---------------------------------------
        const noted::canvas::Camera& camera;
        noted::ui::ImGuiHost& imgui_host;
    };

    [[nodiscard]] static auto create(const Deps& deps) -> std::unique_ptr<RenderPasses>;

    RenderPasses(const RenderPasses&) = delete;
    auto operator=(const RenderPasses&) -> RenderPasses& = delete;
    RenderPasses(RenderPasses&&) = delete;
    auto operator=(RenderPasses&&) -> RenderPasses& = delete;
    ~RenderPasses() = default;

    // Run the per-frame canvas pipeline. Returns:
    //   - `Result<void>{}` on success.
    //   - `gpu_swapchain_out_of_date` / `gpu_swapchain_suboptimal` —
    //     recoverable: caller (App) must wait_idle + recreate the
    //     swapchain + re-bind descriptors, then retry next frame.
    //   - Any other error code — fatal; the caller logs + exits.
    [[nodiscard]] auto render_frame() -> noted::Result<void>;

private:
    explicit RenderPasses(const Deps& deps) noexcept;

    void record_canvas_pass(VkCommandBuffer cb, VkExtent2D ext);
    void record_strokes_pass(VkCommandBuffer cb, VkExtent2D ext);
    void record_overlay_pass(VkCommandBuffer cb, VkExtent2D ext);
    void record_swapchain_pass(VkCommandBuffer cb, VkExtent2D ext);

    // References mirror the Deps struct one-to-one. Kept as separate
    // members (not a Deps value) so the reference-binding rule lets
    // the constructor initialize them in the member-initializer list.
    const noted::gpu::Device& device_;
    const noted::gpu::Swapchain& swapchain_;
    noted::gpu::Renderer& renderer_;
    noted::gpu::CanvasRenderTarget& canvas_;
    noted::gpu::StrokeTarget& strokes_target_;

    noted::canvas::PageRenderer& page_renderer_;
    noted::app::DocumentSession& session_;
    noted::compositor::LayerCompositor& layer_compositor_;
    const noted::app::DemoScene& scene_;

    noted::stroke::StrokeEngine& stroke_engine_;

    const noted::gpu::PipelineLayout& composite_pipeline_layout_;
    const noted::gpu::GraphicsPipeline& composite_pipeline_;
    const noted::gpu::GraphicsPipeline& composite_overlay_pipeline_;

    const noted::gpu::DescriptorSet& canvas_set_;
    const noted::gpu::DescriptorSet& strokes_set_;

    const noted::canvas::Camera& camera_;
    noted::ui::ImGuiHost& imgui_host_;
};

}  // namespace noted::app::frame
