#include "noted/engine/gpu/command_buffer.hpp"

#include <string>

namespace noted::gpu {

auto CommandBuffer::allocate(const CommandPool& pool) -> Result<CommandBuffer> {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool.handle();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer raw = VK_NULL_HANDLE;
    if (auto vr = vkAllocateCommandBuffers(pool.device(), &ai, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkAllocateCommandBuffers: "} + std::to_string(static_cast<int>(vr))));
    }

    CommandBuffer cb;
    cb.device_ = pool.device();
    cb.pool_   = pool.handle();
    cb.handle_ = raw;
    return cb;
}

CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : device_(other.device_), pool_(other.pool_), handle_(other.handle_) {
    other.device_ = VK_NULL_HANDLE;
    other.pool_   = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto CommandBuffer::operator=(CommandBuffer&& other) noexcept -> CommandBuffer& {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        pool_   = other.pool_;
        handle_ = other.handle_;
        other.device_ = VK_NULL_HANDLE;
        other.pool_   = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

CommandBuffer::~CommandBuffer() { destroy(); }

void CommandBuffer::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_, pool_, 1, &handle_);
    }
    device_ = VK_NULL_HANDLE;
    pool_   = VK_NULL_HANDLE;
    handle_ = VK_NULL_HANDLE;
}

auto CommandBuffer::begin(VkCommandBufferUsageFlags flags) -> Result<void> {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = flags;
    if (auto vr = vkBeginCommandBuffer(handle_, &bi); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkBeginCommandBuffer: "} + std::to_string(static_cast<int>(vr))));
    }
    return {};
}

auto CommandBuffer::end() -> Result<void> {
    if (auto vr = vkEndCommandBuffer(handle_); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkEndCommandBuffer: "} + std::to_string(static_cast<int>(vr))));
    }
    return {};
}

void CommandBuffer::reset(VkCommandBufferResetFlags flags) noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        (void)vkResetCommandBuffer(handle_, flags);
    }
}

}  // namespace noted::gpu
