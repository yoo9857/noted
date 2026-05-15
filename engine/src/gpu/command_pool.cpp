#include "noted/engine/gpu/command_pool.hpp"

#include <string>

namespace noted::gpu {

auto CommandPool::create(
    const Device&            device,
    std::uint32_t            queue_family,
    VkCommandPoolCreateFlags flags) -> Result<CommandPool> {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags            = flags;
    ci.queueFamilyIndex = queue_family;

    VkCommandPool raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateCommandPool(device.handle(), &ci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateCommandPool: "} + std::to_string(static_cast<int>(vr))));
    }

    CommandPool p;
    p.owner_  = device.handle();
    p.handle_ = raw;
    return p;
}

CommandPool::CommandPool(CommandPool&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_  = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto CommandPool::operator=(CommandPool&& other) noexcept -> CommandPool& {
    if (this != &other) {
        destroy();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

CommandPool::~CommandPool() { destroy(); }

void CommandPool::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_  = VK_NULL_HANDLE;
}

void CommandPool::reset(VkCommandPoolResetFlags flags) noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        (void)vkResetCommandPool(owner_, handle_, flags);
    }
}

}  // namespace noted::gpu
