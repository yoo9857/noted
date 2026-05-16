#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// Per-thread command pool. Vulkan command pools are not thread-safe, so we
// allocate one per worker thread (the renderer holds N pools — one per
// frame-in-flight).
//
// The default flag set is VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
// which lets us call vkResetCommandBuffer between frames without freeing
// and reallocating.
class CommandPool {
public:
    [[nodiscard]] static auto create(
        const Device& device,
        std::uint32_t queue_family,
        VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
        -> Result<CommandPool>;

    CommandPool(CommandPool&& other) noexcept;
    auto operator=(CommandPool&& other) noexcept -> CommandPool&;
    CommandPool(const CommandPool&) = delete;
    auto operator=(const CommandPool&) -> CommandPool& = delete;
    ~CommandPool();

    [[nodiscard]] auto handle() const noexcept -> VkCommandPool { return handle_; }
    [[nodiscard]] auto device() const noexcept -> VkDevice { return owner_; }

    // Reset every command buffer allocated from this pool back to the
    // initial state. Cheaper than vkResetCommandBuffer per buffer when
    // the whole frame is being recycled.
    void reset(VkCommandPoolResetFlags flags = 0) noexcept;

private:
    CommandPool() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkCommandPool handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
