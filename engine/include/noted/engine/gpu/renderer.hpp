#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/command_buffer.hpp"
#include "noted/engine/gpu/command_pool.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/frame_sync.hpp"
#include "noted/engine/gpu/swapchain.hpp"

namespace noted::gpu {

class CanvasRenderTarget;

// Orchestrates the acquire → record → submit → present cycle.
//
// Frames-in-flight: the renderer rotates through `frames_in_flight` slots
// (default 2). Each slot owns its own CommandPool, CommandBuffer, and
// FrameSync. When the CPU is preparing slot N+1, the GPU may still be
// rendering slot N — the fence in slot N+1 makes the CPU wait if it laps
// the GPU.
//
// render_frame returns an Error with code:
//   gpu_swapchain_out_of_date — the swapchain needs to be recreated before
//                                the next frame can be submitted. Caller
//                                must wait_idle, recreate, and re-issue.
//   gpu_swapchain_suboptimal  — same recovery, but rendering succeeded
//                                this frame.
//
// Any other Error is fatal.
class Renderer {
public:
    [[nodiscard]] static auto create(const Device& device,
                                     const Swapchain& swapchain,
                                     std::uint32_t frames_in_flight = 2) -> Result<Renderer>;

    Renderer(Renderer&& other) noexcept;
    auto operator=(Renderer&& other) noexcept -> Renderer&;
    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    ~Renderer();

    // Run one frame: acquire image, record a layout transition +
    // vkCmdClearColorImage + transition back to PRESENT, submit, present.
    // Returns void on success or an Error (see class doc for recoverable codes).
    [[nodiscard]] auto render_frame(const Device& device,
                                    const Swapchain& swapchain,
                                    VkClearColorValue color) -> Result<void>;

    // Like render_frame, but instead of just clearing, the caller supplies a
    // draw_callback that records pipeline-bound work between
    // vkCmdBeginRendering and vkCmdEndRendering. The callback receives the
    // current frame's command buffer and the swapchain extent.
    //
    // The renderer takes care of layout transitions (UNDEFINED -> COLOR_ATTACHMENT
    // -> PRESENT_SRC), viewport / scissor (set from the extent), and the
    // load-op clear.
    using DrawCallback = std::function<void(VkCommandBuffer cb, VkExtent2D extent)>;

    [[nodiscard]] auto render_frame_with(const Device& device,
                                         const Swapchain& swapchain,
                                         VkClearColorValue clear_color,
                                         const DrawCallback& draw_callback) -> Result<void>;

    // Two-pass canvas pipeline:
    //   1) Canvas pass — caller's `canvas_pass.draw` records into the
    //      offscreen canvas. The renderer transitions the canvas to
    //      COLOR_ATTACHMENT_OPTIMAL, begins dynamic rendering with the
    //      caller-supplied clear color, invokes the callback, ends
    //      rendering, and transitions the canvas to SHADER_READ_ONLY_OPTIMAL.
    //   2) Swapchain pass — exactly the existing render_frame_with body.
    //      Callers typically bind a descriptor sampling `canvas.view()`
    //      and draw a fullscreen quad.
    //
    // Both passes run inside the same command buffer / same submit, so
    // the canvas-to-swapchain handoff is one barrier away — no extra
    // semaphore needed.
    //
    // Same recoverable Error codes as render_frame_with (OUT_OF_DATE /
    // SUBOPTIMAL).
    struct CanvasPassDesc {
        VkClearColorValue clear{};
        DrawCallback draw{};  // records into the canvas
    };
    struct SwapchainPassDesc {
        VkClearColorValue clear{};
        DrawCallback draw{};  // composites canvas onto the swapchain
    };
    [[nodiscard]] auto render_with_canvas(const Device& device,
                                          const Swapchain& swapchain,
                                          CanvasRenderTarget& canvas,
                                          const CanvasPassDesc& canvas_pass,
                                          const SwapchainPassDesc& swapchain_pass) -> Result<void>;

    // Tell the renderer the swapchain has been recreated. Internal per-image
    // resources that depend on image count are rebuilt.
    [[nodiscard]] auto rebind_swapchain(const Device& device,
                                        const Swapchain& swapchain) -> Result<void>;

    [[nodiscard]] auto frames_in_flight() const noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(frames_.size());
    }

    // Public so the implementation-file factory build_slot() (anonymous
    // namespace in renderer.cpp) can name the type. Treat as an
    // implementation detail; no API guarantees on the field layout.
    struct FrameSlot {
        CommandPool pool;
        CommandBuffer cb;
        FrameSync sync;
    };

private:
    Renderer() = default;
    void destroy() noexcept;

    // Tear down + rebuild the per-image render_finished semaphore array
    // to match the current swapchain's image count.
    [[nodiscard]] auto rebuild_present_semaphores(const Device& device,
                                                  const Swapchain& swapchain) -> Result<void>;

    VkDevice owner_ = VK_NULL_HANDLE;
    std::vector<FrameSlot> frames_;
    // One render_finished semaphore per swapchain image. Indexed by the
    // image index returned from vkAcquireNextImageKHR. Per-image (not
    // per-frame-in-flight) so the present queue never re-signals a
    // semaphore that's still in use — see ADR 0008 follow-up.
    std::vector<VkSemaphore> render_finished_per_image_;
    std::uint64_t frame_counter_ = 0;
};

}  // namespace noted::gpu
