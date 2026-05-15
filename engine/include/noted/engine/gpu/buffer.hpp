#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"

namespace noted::gpu {

enum class MemoryUsage : std::uint8_t {
    // GPU-only (vertex/index/uniform/storage on device-local).
    gpu_only,
    // Persistently-mapped, CPU writes + GPU reads. Staging uploads.
    cpu_to_gpu,
    // GPU writes + CPU reads. Readbacks.
    gpu_to_cpu,
};

struct BufferCreateInfo {
    VkDeviceSize       size  = 0;
    VkBufferUsageFlags usage = 0;
    MemoryUsage        memory = MemoryUsage::gpu_only;
    // When true, the allocation is forced to be HOST_VISIBLE + HOST_COHERENT
    // and permanently mapped; map() returns the cached pointer.
    bool               persistent_map = false;
};

// VkBuffer + VmaAllocation. Move-only RAII.
class Buffer {
public:
    [[nodiscard]] static auto create(const Allocator& allocator,
                                     const BufferCreateInfo& info) -> Result<Buffer>;

    Buffer(Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;
    Buffer(const Buffer&) = delete;
    auto operator=(const Buffer&) -> Buffer& = delete;
    ~Buffer();

    [[nodiscard]] auto handle() const noexcept -> VkBuffer { return handle_; }
    [[nodiscard]] auto size() const noexcept -> VkDeviceSize { return size_; }
    [[nodiscard]] auto mapped() const noexcept -> void* { return mapped_; }
    [[nodiscard]] auto allocation() const noexcept -> VmaAllocation { return allocation_; }

    // Copy a host span into the buffer. Requires either persistent_map=true
    // or memory=cpu_to_gpu so the allocation is HOST_VISIBLE.
    [[nodiscard]] auto write(std::span<const std::byte> bytes,
                             VkDeviceSize offset = 0) -> Result<void>;

private:
    Buffer() = default;
    void destroy() noexcept;

    VmaAllocator   owner_      = nullptr;
    VkBuffer       handle_     = VK_NULL_HANDLE;
    VmaAllocation  allocation_ = nullptr;
    VkDeviceSize   size_       = 0;
    void*          mapped_     = nullptr;
};

}  // namespace noted::gpu
