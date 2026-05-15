#pragma once

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/command_pool.hpp"

namespace noted::gpu {

// Single primary command buffer allocated from a CommandPool.
//
// Lifetime is owned by the pool: when the pool is destroyed, the buffer
// becomes invalid. CommandBuffer therefore keeps a non-owning reference
// to its pool's VkDevice and VkCommandPool, and the destructor calls
// vkFreeCommandBuffers explicitly so we don't leak when the pool outlives
// the buffer.
class CommandBuffer {
public:
    [[nodiscard]] static auto allocate(const CommandPool& pool) -> Result<CommandBuffer>;

    CommandBuffer(CommandBuffer&& other) noexcept;
    auto operator=(CommandBuffer&& other) noexcept -> CommandBuffer&;
    CommandBuffer(const CommandBuffer&) = delete;
    auto operator=(const CommandBuffer&) -> CommandBuffer& = delete;
    ~CommandBuffer();

    [[nodiscard]] auto handle() const noexcept -> VkCommandBuffer { return handle_; }

    // Begin recording. flags is typically 0 for re-recordable buffers or
    // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT for one-shot uploads.
    [[nodiscard]] auto begin(VkCommandBufferUsageFlags flags = 0) -> Result<void>;

    [[nodiscard]] auto end() -> Result<void>;

    void reset(VkCommandBufferResetFlags flags = 0) noexcept;

private:
    CommandBuffer() = default;
    void destroy() noexcept;

    VkDevice        device_ = VK_NULL_HANDLE;
    VkCommandPool   pool_   = VK_NULL_HANDLE;
    VkCommandBuffer handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
