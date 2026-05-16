// Single VMA implementation TU for the entire engine.
//
// VMA is a header-only library. Defining VMA_IMPLEMENTATION before the
// include emits the function bodies; every other TU includes
// <vk_mem_alloc.h> as a normal header.
#define VMA_IMPLEMENTATION

#include "noted/engine/gpu/allocator.hpp"

#include <string>

namespace noted::gpu {

auto Allocator::create(const Instance& instance,
                       const PhysicalDevice& physical,
                       const Device& device,
                       std::uint32_t api_version) -> Result<Allocator> {
    VmaAllocatorCreateInfo ci{};
    ci.physicalDevice = physical.handle();
    ci.device = device.handle();
    ci.instance = instance.handle();
    ci.vulkanApiVersion = api_version;

    VmaAllocator raw = nullptr;
    if (auto vr = vmaCreateAllocator(&ci, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vmaCreateAllocator: "} + std::to_string(static_cast<int>(vr))));
    }

    Allocator a;
    a.device_ = device.handle();
    a.handle_ = raw;
    return a;
}

Allocator::Allocator(Allocator&& other) noexcept : device_(other.device_), handle_(other.handle_) {
    other.device_ = VK_NULL_HANDLE;
    other.handle_ = nullptr;
}

auto Allocator::operator=(Allocator&& other) noexcept -> Allocator& {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        handle_ = other.handle_;
        other.device_ = VK_NULL_HANDLE;
        other.handle_ = nullptr;
    }
    return *this;
}

Allocator::~Allocator() {
    destroy();
}

void Allocator::destroy() noexcept {
    if (handle_ != nullptr) {
        vmaDestroyAllocator(handle_);
        handle_ = nullptr;
    }
    device_ = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
