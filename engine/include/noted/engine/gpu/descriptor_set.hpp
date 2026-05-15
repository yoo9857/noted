#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// Non-owning wrapper around a VkDescriptorSet allocated from a pool.
// The pool owns destruction.
//
// `Writer` is a tiny builder that batches descriptor writes; one
// vkUpdateDescriptorSets call commits all queued writes at once.
class DescriptorSet {
public:
    DescriptorSet() = default;
    DescriptorSet(VkDevice device, VkDescriptorSet handle) noexcept
        : device_(device), handle_(handle) {}

    [[nodiscard]] auto handle() const noexcept -> VkDescriptorSet { return handle_; }
    [[nodiscard]] auto device() const noexcept -> VkDevice { return device_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != VK_NULL_HANDLE;
    }

private:
    VkDevice        device_ = VK_NULL_HANDLE;
    VkDescriptorSet handle_ = VK_NULL_HANDLE;
};

class DescriptorWriter {
public:
    explicit DescriptorWriter(DescriptorSet set) noexcept : set_(set) {}

    // Bind an Image + Sampler as a combined sampler at the given binding.
    auto write_combined_image_sampler(
        std::uint32_t binding,
        VkImageView   view,
        VkSampler     sampler,
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        std::uint32_t array_element = 0) -> DescriptorWriter&;

    // Bind a Buffer range as a uniform buffer at the given binding.
    auto write_uniform_buffer(
        std::uint32_t binding,
        VkBuffer      buffer,
        VkDeviceSize  offset = 0,
        VkDeviceSize  range  = VK_WHOLE_SIZE,
        std::uint32_t array_element = 0) -> DescriptorWriter&;

    // Bind a Buffer range as a storage buffer.
    auto write_storage_buffer(
        std::uint32_t binding,
        VkBuffer      buffer,
        VkDeviceSize  offset = 0,
        VkDeviceSize  range  = VK_WHOLE_SIZE,
        std::uint32_t array_element = 0) -> DescriptorWriter&;

    // Bind a storage image at the given binding.
    auto write_storage_image(
        std::uint32_t binding,
        VkImageView   view,
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL,
        std::uint32_t array_element = 0) -> DescriptorWriter&;

    // Commit all queued writes via vkUpdateDescriptorSets.
    void commit();

private:
    DescriptorSet                    set_;
    // Storage stays valid until commit() because pImageInfo / pBufferInfo
    // are pointers into these vectors. We use vector::reserve to keep
    // pointers stable across the batched writes.
    std::vector<VkDescriptorImageInfo>   image_infos_;
    std::vector<VkDescriptorBufferInfo>  buffer_infos_;
    std::vector<VkWriteDescriptorSet>    writes_;

    enum class InfoKind : std::uint8_t { image, buffer };
    struct Pending {
        InfoKind kind{};
        std::size_t index{};
    };
    std::vector<Pending> pending_;
};

}  // namespace noted::gpu
