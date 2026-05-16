#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/image.hpp"

namespace noted::gpu {

struct UploadImageInfo {
    // Caller picks the queue. For textures uploaded once at load time, the
    // graphics queue is fine; for streaming uploads use a dedicated transfer
    // queue if available.
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = UINT32_MAX;
    // Layout the image is currently in. UNDEFINED is the typical case for
    // a freshly-created Image.
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Layout the image should be in when this call returns. SHADER_READ
    // for sampled textures.
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    std::uint32_t mip_level = 0;
    std::uint32_t base_array_layer = 0;
    std::uint32_t layer_count = 1;
};

// Synchronously upload `pixels` into `dst` using a temporary staging buffer
// and a one-shot command buffer. Blocks until the GPU has consumed the
// data, then returns.
//
// pixels.size_bytes() must equal width*height*depth*bytes_per_texel for the
// image's format. The function does not infer texel size — the caller
// computes it for the layout that matches the source data.
//
// For high-throughput uploads (streaming many tiles per frame) use a
// dedicated TransferScheduler that batches into one command buffer and
// signals timeline semaphores. That lives in feat/transfer-scheduler.
[[nodiscard]] auto upload_image_pixels(const Allocator& allocator,
                                       const Device& device,
                                       Image& dst,
                                       std::span<const std::byte> pixels,
                                       const UploadImageInfo& info) -> Result<void>;

}  // namespace noted::gpu
