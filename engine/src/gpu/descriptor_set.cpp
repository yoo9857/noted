#include "noted/engine/gpu/descriptor_set.hpp"

namespace noted::gpu {

namespace {

constexpr std::size_t kReserve = 32;

void enqueue_write(
    std::vector<VkWriteDescriptorSet>& writes,
    VkDescriptorSet  dst_set,
    std::uint32_t    binding,
    std::uint32_t    array_element,
    VkDescriptorType type,
    std::uint32_t    count) {
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = dst_set;
    w.dstBinding      = binding;
    w.dstArrayElement = array_element;
    w.descriptorCount = count;
    w.descriptorType  = type;
    writes.push_back(w);
}

}  // namespace

auto DescriptorWriter::write_combined_image_sampler(
    std::uint32_t binding,
    VkImageView   view,
    VkSampler     sampler,
    VkImageLayout layout,
    std::uint32_t array_element) -> DescriptorWriter& {
    if (image_infos_.capacity() < kReserve) {
        image_infos_.reserve(kReserve);
    }
    image_infos_.push_back(VkDescriptorImageInfo{
        .sampler     = sampler,
        .imageView   = view,
        .imageLayout = layout,
    });
    enqueue_write(writes_, set_.handle(), binding, array_element,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    pending_.push_back({InfoKind::image, image_infos_.size() - 1});
    return *this;
}

auto DescriptorWriter::write_uniform_buffer(
    std::uint32_t binding,
    VkBuffer      buffer,
    VkDeviceSize  offset,
    VkDeviceSize  range,
    std::uint32_t array_element) -> DescriptorWriter& {
    if (buffer_infos_.capacity() < kReserve) {
        buffer_infos_.reserve(kReserve);
    }
    buffer_infos_.push_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range  = range,
    });
    enqueue_write(writes_, set_.handle(), binding, array_element,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
    pending_.push_back({InfoKind::buffer, buffer_infos_.size() - 1});
    return *this;
}

auto DescriptorWriter::write_storage_buffer(
    std::uint32_t binding,
    VkBuffer      buffer,
    VkDeviceSize  offset,
    VkDeviceSize  range,
    std::uint32_t array_element) -> DescriptorWriter& {
    if (buffer_infos_.capacity() < kReserve) {
        buffer_infos_.reserve(kReserve);
    }
    buffer_infos_.push_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range  = range,
    });
    enqueue_write(writes_, set_.handle(), binding, array_element,
                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
    pending_.push_back({InfoKind::buffer, buffer_infos_.size() - 1});
    return *this;
}

auto DescriptorWriter::write_storage_image(
    std::uint32_t binding,
    VkImageView   view,
    VkImageLayout layout,
    std::uint32_t array_element) -> DescriptorWriter& {
    if (image_infos_.capacity() < kReserve) {
        image_infos_.reserve(kReserve);
    }
    image_infos_.push_back(VkDescriptorImageInfo{
        .sampler     = VK_NULL_HANDLE,
        .imageView   = view,
        .imageLayout = layout,
    });
    enqueue_write(writes_, set_.handle(), binding, array_element,
                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
    pending_.push_back({InfoKind::image, image_infos_.size() - 1});
    return *this;
}

void DescriptorWriter::commit() {
    if (writes_.empty() || set_.device() == VK_NULL_HANDLE) {
        return;
    }
    // Resolve pointers now that all infos are settled. The vectors must
    // not reallocate between the loops; the kReserve up-front avoids that
    // for typical descriptor counts.
    for (std::size_t i = 0; i < writes_.size(); ++i) {
        const auto& p = pending_[i];
        if (p.kind == InfoKind::image) {
            writes_[i].pImageInfo  = &image_infos_[p.index];
        } else {
            writes_[i].pBufferInfo = &buffer_infos_[p.index];
        }
    }
    vkUpdateDescriptorSets(set_.device(),
                           static_cast<std::uint32_t>(writes_.size()),
                           writes_.data(), 0, nullptr);
    writes_.clear();
    image_infos_.clear();
    buffer_infos_.clear();
    pending_.clear();
}

}  // namespace noted::gpu
