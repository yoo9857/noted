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
    [[nodiscard]] static auto create(
        const Device&  device,
        const Swapchain& swapchain,
        std::uint32_t  frames_in_flight = 2) -> Result<Renderer>;

    Renderer(Renderer&& other) noexcept;
    auto operator=(Renderer&& other) noexcept -> Renderer&;
    Renderer(const Renderer&) = delete;
    auto operator=(const Renderer&) -> Renderer& = delete;
    ~Renderer();

    // Run one frame: acquire image, record a layout transition +
    // vkCmdClearColorImage + transition back to PRESENT, submit, present.
    // Returns void on success or an Error (see class doc for recoverable codes).
    [[nodiscard]] auto render_frame(
        const Device&    device,
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

    [[nodiscard]] auto render_frame_with(
        const Device&    device,
        const Swapchain& swapchain,
        VkClearColorValue clear_color,
        const DrawCallback& draw_callback) -> Result<void>;

    // Tell the renderer the swapchain has been recreated. Internal per-image
    // resources that depend on image count are rebuilt.
    [[nodiscard]] auto rebind_swapchain(
        const Device&    device,
        const Swapchain& swapchain) -> Result<void>;

    [[nodiscard]] auto frames_in_flight() const noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(frames_.size());
    }

    // Public so the implementation-file factory build_slot() (anonymous
    // namespace in renderer.cpp) can name the type. Treat as an
    // implementation detail; no API guarantees on the field layout.
    struct FrameSlot {
        CommandPool   pool;
        CommandBuffer cb;
        FrameSync     sync;
    };

private:
    Renderer() = default;
    void destroy() noexcept;

    VkDevice              owner_         = VK_NULL_HANDLE;
    std::vector<FrameSlot> frames_;
    std::uint64_t          frame_counter_ = 0;
};

}  // namespace noted::gpu
