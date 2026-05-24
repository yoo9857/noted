#include "noted/compositor/image_asset_gpu_registry.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>

#include <backends/imgui_impl_vulkan.h>

#include "noted/domain/document/image_asset_registry.hpp"
#include "noted/engine/gpu/upload.hpp"
#include "noted/platform/image_io/image_io.hpp"

namespace noted::compositor {

namespace {

// Single LINEAR sampler shared across every asset. We don't expose
// per-asset filter settings yet — see ADR 0037 for the rationale.
[[nodiscard]] auto create_shared_sampler(VkDevice device) -> noted::Result<VkSampler> {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.minLod = 0.0F;
    info.maxLod = 0.0F;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    if (auto vr = vkCreateSampler(device, &info, nullptr, &sampler); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            std::string{"ImageAssetGpuRegistry: vkCreateSampler failed: VkResult="} +
                std::to_string(vr)));
    }
    return sampler;
}

}  // namespace

auto ImageAssetGpuRegistry::create(const noted::gpu::Device& device,
                                   const noted::gpu::Allocator& allocator)
    -> noted::Result<ImageAssetGpuRegistry> {
    auto sampler = create_shared_sampler(device.handle());
    if (!sampler) {
        return std::unexpected(std::move(sampler).error());
    }
    ImageAssetGpuRegistry reg;
    reg.device_ = &device;
    reg.allocator_ = &allocator;
    reg.sampler_ = *sampler;
    return reg;
}

ImageAssetGpuRegistry::ImageAssetGpuRegistry(ImageAssetGpuRegistry&& other) noexcept
    : device_(other.device_),
      allocator_(other.allocator_),
      sampler_(other.sampler_),
      live_(std::move(other.live_)),
      retired_(std::move(other.retired_)),
      frame_counter_(other.frame_counter_) {
    other.device_ = nullptr;
    other.allocator_ = nullptr;
    other.sampler_ = VK_NULL_HANDLE;
    other.frame_counter_ = 0;
}

auto ImageAssetGpuRegistry::operator=(ImageAssetGpuRegistry&& other) noexcept
    -> ImageAssetGpuRegistry& {
    if (this != &other) {
        shutdown();
        device_ = other.device_;
        allocator_ = other.allocator_;
        sampler_ = other.sampler_;
        live_ = std::move(other.live_);
        retired_ = std::move(other.retired_);
        frame_counter_ = other.frame_counter_;
        other.device_ = nullptr;
        other.allocator_ = nullptr;
        other.sampler_ = VK_NULL_HANDLE;
        other.frame_counter_ = 0;
    }
    return *this;
}

ImageAssetGpuRegistry::~ImageAssetGpuRegistry() {
    shutdown();
}

void ImageAssetGpuRegistry::release_entry_(Entry& e) noexcept {
    if (e.imgui_ds != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(e.imgui_ds);
        e.imgui_ds = VK_NULL_HANDLE;
    }
    // The gpu::Image destructor frees VkImage + VkImageView +
    // VmaAllocation. Nothing more to do here.
}

void ImageAssetGpuRegistry::shutdown() noexcept {
    for (auto& [id, entry] : live_) {
        release_entry_(entry);
    }
    live_.clear();
    for (auto& r : retired_) {
        release_entry_(r.entry);
    }
    retired_.clear();
    if (sampler_ != VK_NULL_HANDLE && device_ != nullptr) {
        vkDestroySampler(device_->handle(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    device_ = nullptr;
    allocator_ = nullptr;
}

auto ImageAssetGpuRegistry::upload(const ImageUploadInfo& info) -> noted::Result<void> {
    if (info.id == noted::domain::invalid_asset_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument, "ImageAssetGpuRegistry::upload: invalid_asset_id"));
    }
    if (info.width == 0 || info.height == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "ImageAssetGpuRegistry::upload: zero-extent image (" + std::to_string(info.width) +
                "x" + std::to_string(info.height) + ")"));
    }
    const auto expected_bytes =
        static_cast<std::size_t>(info.width) * static_cast<std::size_t>(info.height) * 4U;
    if (info.rgba_byte_count != expected_bytes || info.rgba_pixels == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "ImageAssetGpuRegistry::upload: byte count " + std::to_string(info.rgba_byte_count) +
                " != expected " + std::to_string(expected_bytes) + " for RGBA8 " +
                std::to_string(info.width) + "x" + std::to_string(info.height)));
    }

    // Allocate the destination image.
    noted::gpu::ImageCreateInfo image_info{};
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent = {info.width, info.height, 1};
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto image = noted::gpu::Image::create(*allocator_, image_info);
    if (!image) {
        return std::unexpected(std::move(image).error());
    }

    // Synchronous staging upload — blocks until the GPU has consumed
    // the bytes and the final SHADER_READ_ONLY_OPTIMAL transition has
    // retired. Acceptable for v0.x's one-image-at-a-time picker flow;
    // an async / batched path is `feat/async-asset-loading` work.
    noted::gpu::UploadImageInfo upload_info{};
    upload_info.queue = device_->graphics_queue();
    upload_info.queue_family = device_->graphics_family();
    upload_info.final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (auto r = noted::gpu::upload_image_pixels(
            *allocator_,
            *device_,
            *image,
            std::span<const std::byte>(info.rgba_pixels, info.rgba_byte_count),
            upload_info);
        !r) {
        return std::unexpected(std::move(r).error());
    }

    // Register with ImGui's Vulkan backend so the draw list can
    // reference the texture by descriptor set.
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        sampler_, image->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (ds == VK_NULL_HANDLE) {
        // gpu::Image dies via RAII on return; nothing to clean up.
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "ImageAssetGpuRegistry::upload: ImGui_ImplVulkan_AddTexture returned VK_NULL_HANDLE"));
    }

    Entry new_entry;
    new_entry.image.emplace(std::move(*image));
    new_entry.imgui_ds = ds;

    // If an entry already exists for this id, retire it. The new
    // upload replaces it visibly this frame; the old GPU resources
    // get freed after the in-flight delay has elapsed.
    if (auto it = live_.find(info.id); it != live_.end()) {
        retired_.push_back(Retired{.entry = std::move(it->second),
                                   .release_after_tick = frame_counter_ + kRetireDelayFrames});
        live_.erase(it);
    }
    live_.emplace(info.id, std::move(new_entry));
    return {};
}

auto ImageAssetGpuRegistry::remove(noted::domain::AssetId id) -> bool {
    auto it = live_.find(id);
    if (it == live_.end()) {
        return false;
    }
    retired_.push_back(Retired{.entry = std::move(it->second),
                               .release_after_tick = frame_counter_ + kRetireDelayFrames});
    live_.erase(it);
    return true;
}

auto ImageAssetGpuRegistry::texture_for(noted::domain::AssetId id) const noexcept
    -> VkDescriptorSet {
    auto it = live_.find(id);
    return it == live_.end() ? VK_NULL_HANDLE : it->second.imgui_ds;
}

auto ImageAssetGpuRegistry::has(noted::domain::AssetId id) const noexcept -> bool {
    return live_.find(id) != live_.end();
}

auto ImageAssetGpuRegistry::live_ids() const -> std::vector<noted::domain::AssetId> {
    std::vector<noted::domain::AssetId> ids;
    ids.reserve(live_.size());
    for (const auto& [id, entry] : live_) {
        ids.push_back(id);
    }
    return ids;
}

void ImageAssetGpuRegistry::tick() {
    frame_counter_++;
    // Release retired entries whose delay has elapsed. erase-remove
    // pattern via partition to keep the still-pending ones at the
    // front.
    const auto kept_end =
        std::stable_partition(retired_.begin(), retired_.end(), [this](const Retired& r) noexcept {
            return r.release_after_tick > frame_counter_;
        });
    for (auto it = kept_end; it != retired_.end(); ++it) {
        release_entry_(it->entry);
    }
    retired_.erase(kept_end, retired_.end());
}

auto sync_image_assets(ImageAssetGpuRegistry& gpu_registry,
                       const noted::domain::ImageAssetRegistry& source) -> noted::Result<void> {
    // First pass: upload anything in `source` missing from GPU.
    for (const auto& asset : source.assets()) {
        if (gpu_registry.has(asset.id)) {
            continue;
        }
        if (asset.source_bytes.empty()) {
            // Placeholder asset (never picked from disk, or v6..v9
            // legacy file). No bytes to decode; skip silently — the
            // overlay falls back to the placeholder rect.
            continue;
        }
        auto decoded = noted::platform::image_io::decode_rgba8(asset.source_bytes);
        if (!decoded) {
            // Per-asset decode failures are logged but non-fatal: a
            // corrupt asset shouldn't stop healthy assets from
            // uploading.
            std::cerr << "sync_image_assets: decode failed for asset " << asset.id << ": "
                      << decoded.error().format() << '\n';
            continue;
        }
        ImageUploadInfo upload{};
        upload.id = asset.id;
        upload.width = decoded->width;
        upload.height = decoded->height;
        upload.rgba_pixels = decoded->pixels.data();
        upload.rgba_byte_count = decoded->pixels.size();
        if (auto r = gpu_registry.upload(upload); !r) {
            return std::unexpected(std::move(r).error());
        }
    }

    // Second pass: drop GPU entries whose AssetId no longer appears
    // in the domain registry (the user removed the corresponding
    // primitive + asset). O(N + M·log M) via a sorted lookup set;
    // for v0.x's asset counts (~tens) the constants dominate but it
    // keeps the API honest as documents grow.
    std::vector<noted::domain::AssetId> source_ids;
    source_ids.reserve(source.size());
    for (const auto& asset : source.assets()) {
        source_ids.push_back(asset.id);
    }
    std::sort(source_ids.begin(), source_ids.end());
    for (const auto id : gpu_registry.live_ids()) {
        if (!std::binary_search(source_ids.begin(), source_ids.end(), id)) {
            (void) gpu_registry.remove(id);
        }
    }

    return {};
}

}  // namespace noted::compositor
