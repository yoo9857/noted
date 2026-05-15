#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"

namespace noted::gpu {

// RAII wrapper around VmaAllocator.
//
// VMA is a thin layer over Vulkan memory: it picks the right heap type,
// sub-allocates inside large device-memory blocks, and exposes mapping
// helpers that respect coherency / flush requirements.
//
// One Allocator per Device. Move-only.
class Allocator {
public:
    [[nodiscard]] static auto create(
        const Instance&       instance,
        const PhysicalDevice& physical,
        const Device&         device,
        std::uint32_t         api_version = VK_API_VERSION_1_3) -> Result<Allocator>;

    Allocator(Allocator&& other) noexcept;
    auto operator=(Allocator&& other) noexcept -> Allocator&;
    Allocator(const Allocator&) = delete;
    auto operator=(const Allocator&) -> Allocator& = delete;
    ~Allocator();

    [[nodiscard]] auto handle() const noexcept -> VmaAllocator { return handle_; }
    [[nodiscard]] auto device() const noexcept -> VkDevice { return device_; }

private:
    Allocator() = default;
    void destroy() noexcept;

    VkDevice     device_ = VK_NULL_HANDLE;
    VmaAllocator handle_ = nullptr;
};

}  // namespace noted::gpu
