#include "noted/engine/gpu/buffer.hpp"

#include <cstring>
#include <string>

namespace noted::gpu {

namespace {

[[nodiscard]] auto to_vma_usage(MemoryUsage m) noexcept -> VmaMemoryUsage {
    switch (m) {
        case MemoryUsage::gpu_only:   return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        case MemoryUsage::cpu_to_gpu: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        case MemoryUsage::gpu_to_cpu: return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }
    return VMA_MEMORY_USAGE_AUTO;
}

[[nodiscard]] auto to_vma_flags(const BufferCreateInfo& info) noexcept
    -> VmaAllocationCreateFlags {
    VmaAllocationCreateFlags f = 0;
    if (info.persistent_map ||
        info.memory == MemoryUsage::cpu_to_gpu ||
        info.memory == MemoryUsage::gpu_to_cpu) {
        f |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    if (info.persistent_map) {
        f |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    return f;
}

}  // namespace

auto Buffer::create(const Allocator& allocator, const BufferCreateInfo& info)
    -> Result<Buffer> {
    if (info.size == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument, "Buffer::create: size == 0"));
    }

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = info.size;
    bci.usage       = info.usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = to_vma_usage(info.memory);
    aci.flags = to_vma_flags(info);

    VkBuffer       raw_buf  = VK_NULL_HANDLE;
    VmaAllocation  alloc    = nullptr;
    VmaAllocationInfo alloc_info{};
    if (auto vr = vmaCreateBuffer(allocator.handle(), &bci, &aci,
                                  &raw_buf, &alloc, &alloc_info);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_out_of_memory,
            std::string{"vmaCreateBuffer: "} + std::to_string(static_cast<int>(vr))));
    }

    Buffer b;
    b.owner_      = allocator.handle();
    b.handle_     = raw_buf;
    b.allocation_ = alloc;
    b.size_       = info.size;
    b.mapped_     = info.persistent_map ? alloc_info.pMappedData : nullptr;
    return b;
}

Buffer::Buffer(Buffer&& other) noexcept
    : owner_(other.owner_),
      handle_(other.handle_),
      allocation_(other.allocation_),
      size_(other.size_),
      mapped_(other.mapped_) {
    other.owner_      = nullptr;
    other.handle_     = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.size_       = 0;
    other.mapped_     = nullptr;
}

auto Buffer::operator=(Buffer&& other) noexcept -> Buffer& {
    if (this != &other) {
        destroy();
        owner_      = other.owner_;
        handle_     = other.handle_;
        allocation_ = other.allocation_;
        size_       = other.size_;
        mapped_     = other.mapped_;
        other.owner_      = nullptr;
        other.handle_     = VK_NULL_HANDLE;
        other.allocation_ = nullptr;
        other.size_       = 0;
        other.mapped_     = nullptr;
    }
    return *this;
}

Buffer::~Buffer() { destroy(); }

void Buffer::destroy() noexcept {
    if (owner_ != nullptr && handle_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(owner_, handle_, allocation_);
    }
    owner_      = nullptr;
    handle_     = VK_NULL_HANDLE;
    allocation_ = nullptr;
    size_       = 0;
    mapped_     = nullptr;
}

auto Buffer::write(std::span<const std::byte> bytes, VkDeviceSize offset) -> Result<void> {
    if (bytes.size() + offset > size_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "Buffer::write: span + offset exceeds buffer size"));
    }
    if (mapped_ != nullptr) {
        std::memcpy(static_cast<std::byte*>(mapped_) + offset, bytes.data(), bytes.size());
        return {};
    }
    void* mapped = nullptr;
    if (auto vr = vmaMapMemory(owner_, allocation_, &mapped); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vmaMapMemory: "} + std::to_string(static_cast<int>(vr))));
    }
    std::memcpy(static_cast<std::byte*>(mapped) + offset, bytes.data(), bytes.size());
    vmaUnmapMemory(owner_, allocation_);
    return {};
}

}  // namespace noted::gpu
