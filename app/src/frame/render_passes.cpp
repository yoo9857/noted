#include "frame/render_passes.hpp"

#include "noted/compositor/layer_compositor.hpp"
#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/canvas/page_renderer.hpp"
#include "noted/engine/gpu/canvas_render_target.hpp"
#include "noted/engine/gpu/descriptor_set.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/renderer.hpp"
#include "noted/engine/gpu/stroke_target.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"
#include "noted/ui/imgui_host.hpp"

#include "scene/demo_scene.hpp"
#include "ui/document_session.hpp"

namespace noted::app::frame {

namespace {

// Push-constant payload for the composite shader (`shaders/fullscreen.slang`'s
// `CompositePush`). Two float2 — 16 bytes total. Layout pinned by the
// shader; changing it requires updating both sides + bumping the
// pipeline-cache version.
struct CompositePush {
    float scale[2];
    float translation[2];
};
static_assert(sizeof(CompositePush) == 16,
              "CompositePush must mirror shaders/fullscreen.slang CompositePush");

// Clear values are constants per the design — the canvas's paper
// shows over an opaque-black clear, the strokes target is fully
// transparent at frame start so eraser alpha can't leak across
// frames, and the swapchain clears to dark teal as a regression
// tell for the composite quad ever leaving edges blank.
constexpr VkClearColorValue kCanvasClear{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}};
constexpr VkClearColorValue kStrokesClear{.float32 = {0.0F, 0.0F, 0.0F, 0.0F}};
constexpr VkClearColorValue kSwapchainClear{.float32 = {0.05F, 0.05F, 0.10F, 1.0F}};

}  // namespace

RenderPasses::RenderPasses(const Deps& d) noexcept
    : device_(d.device),
      swapchain_(d.swapchain),
      renderer_(d.renderer),
      canvas_(d.canvas),
      strokes_target_(d.strokes_target),
      page_renderer_(d.page_renderer),
      session_(d.session),
      layer_compositor_(d.layer_compositor),
      scene_(d.scene),
      stroke_engine_(d.stroke_engine),
      composite_pipeline_layout_(d.composite_pipeline_layout),
      composite_pipeline_(d.composite_pipeline),
      composite_overlay_pipeline_(d.composite_overlay_pipeline),
      canvas_set_(d.canvas_set),
      strokes_set_(d.strokes_set),
      camera_(d.camera),
      imgui_host_(d.imgui_host) {}

auto RenderPasses::create(const Deps& deps) -> std::unique_ptr<RenderPasses> {
    // Heap-allocated for consistency with the other extracted
    // subsystems (`ToolInputRouter`, `CameraController`). Strictly
    // speaking RenderPasses doesn't subscribe to anything, so an
    // `std::optional<RenderPasses>` on App would work too — but
    // unique_ptr is the established pattern at this layer and the
    // allocation cost is one-time at startup.
    return std::unique_ptr<RenderPasses>(new RenderPasses{deps});
}

auto RenderPasses::render_frame() -> noted::Result<void> {
    auto canvas_cb = [this](VkCommandBuffer cb, VkExtent2D ext) { record_canvas_pass(cb, ext); };
    auto strokes_cb = [this](VkCommandBuffer cb, VkExtent2D ext) { record_strokes_pass(cb, ext); };
    auto overlay_cb = [this](VkCommandBuffer cb, VkExtent2D ext) { record_overlay_pass(cb, ext); };
    auto swapchain_cb = [this](VkCommandBuffer cb, VkExtent2D ext) {
        record_swapchain_pass(cb, ext);
    };

    auto rr = renderer_.render_with_canvas(
        device_,
        swapchain_,
        canvas_,
        strokes_target_,
        noted::gpu::Renderer::CanvasPassDesc{kCanvasClear, canvas_cb},
        noted::gpu::Renderer::StrokesPassDesc{kStrokesClear, strokes_cb},
        noted::gpu::Renderer::OverlayPassDesc{overlay_cb},
        noted::gpu::Renderer::SwapchainPassDesc{kSwapchainClear, swapchain_cb});
    if (!rr) {
        // Recoverable + fatal codes propagate untouched; App's
        // `render_one_frame` wrapper turns the recoverable codes
        // into a `recreate_swapchain` call (owner work — touches
        // the swapchain handle itself + reallocates targets).
        return std::unexpected(std::move(rr).error());
    }
    return {};
}

void RenderPasses::record_canvas_pass(VkCommandBuffer cb, VkExtent2D ext) {
    // Phase 1 of the four-pass canvas pipeline (ADR 0031). Paper +
    // layers go into the canvas; ink lands in a separate strokes
    // target rendered by `record_strokes_pass`.
    //
    // Order matters within this pass:
    //   1. Page backgrounds — paper rectangles. The canvas pass's
    //      clear colour shows through everywhere outside the pages.
    //   2. Layer compositor — adjustment / image layers within
    //      pages (none today; demo payloads were dropped in A.3.b
    //      so pages stay visible).
    page_renderer_.render(cb, ext, session_.document().pages());
    layer_compositor_.composite(cb, ext, scene_.graph, scene_.store);
}

void RenderPasses::record_strokes_pass(VkCommandBuffer cb, VkExtent2D ext) {
    // Phase 2: the stroke engine writes into `strokes_target` (cleared
    // to transparent black each frame by the renderer). Draw strokes
    // accumulate alpha; eraser strokes subtract alpha — both behaviours
    // come from the engine's two pipelines + the active `DrawMode`.
    stroke_engine_.record(cb, ext);
}

void RenderPasses::record_overlay_pass(VkCommandBuffer cb, VkExtent2D /*ext*/) {
    // Phase 3: composite the strokes target onto the canvas with
    // SRC_OVER blend. The canvas attachment is LOAD_OP_LOAD (paper +
    // layers from phase 1 survive); where strokes_target alpha=0, the
    // canvas is unchanged → page pattern shows through erased regions.
    //
    // Identity transform: scale=(2,2), translation=(-1,-1) reproduces
    // the standard `uv → ndc = uv * 2 - 1` mapping, so the strokes
    // target fills the canvas at 1:1 with no camera projection.
    const auto layout_h = composite_pipeline_layout_.handle();
    const auto pipeline_h = composite_overlay_pipeline_.handle();
    const VkDescriptorSet set_h = strokes_set_.handle();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
    vkCmdBindDescriptorSets(cb,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout_h,
                            /*firstSet=*/0,
                            /*setCount=*/1,
                            &set_h,
                            /*dynamicOffsetCount=*/0,
                            nullptr);

    CompositePush push{};
    push.scale[0] = 2.0F;
    push.scale[1] = 2.0F;
    push.translation[0] = -1.0F;
    push.translation[1] = -1.0F;
    vkCmdPushConstants(cb, layout_h, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CompositePush), &push);

    vkCmdDraw(cb, /*vertexCount=*/6, /*instanceCount=*/1, 0, 0);
}

void RenderPasses::record_swapchain_pass(VkCommandBuffer cb, VkExtent2D /*ext*/) {
    // Phase 4: composite the canvas onto the swapchain via the
    // Camera-derived view transform, then overlay ImGui on top.
    const auto layout_h = composite_pipeline_layout_.handle();
    const auto pipeline_h = composite_pipeline_.handle();
    const VkDescriptorSet set_h = canvas_set_.handle();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
    vkCmdBindDescriptorSets(cb,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout_h,
                            /*firstSet=*/0,
                            /*setCount=*/1,
                            &set_h,
                            /*dynamicOffsetCount=*/0,
                            nullptr);

    CompositePush push{};
    push.scale[0] = camera_.shader_scale_x();
    push.scale[1] = camera_.shader_scale_y();
    push.translation[0] = camera_.shader_translation_x();
    push.translation[1] = camera_.shader_translation_y();
    vkCmdPushConstants(cb, layout_h, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CompositePush), &push);

    // 6 vertices = a quad (two triangles, uv ∈ [0,1]²). See
    // `shaders/fullscreen.slang` for why the classic fullscreen-
    // triangle pattern was rejected — camera scale < 1 shrinks the
    // triangle's NDC bounding box and exposes a triangular cut.
    vkCmdDraw(cb, /*vertexCount=*/6, /*instanceCount=*/1, 0, 0);

    // ImGui draws on top of the composited canvas. The surrounding
    // vkCmdBeginRendering (owned by the renderer) is the right
    // context for ImGui_ImplVulkan_RenderDrawData. finalize_frame()
    // was already called above; render_into just records the cached
    // draw data.
    imgui_host_.render_into(cb);
}

}  // namespace noted::app::frame
