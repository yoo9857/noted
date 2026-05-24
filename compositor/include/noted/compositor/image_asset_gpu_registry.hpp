#pragma once

// ImageAssetGpuRegistry — the GPU-side companion to
// `noted::domain::ImageAssetRegistry` (domain).
//
// Phase B.7.b.2b. The domain registry persists `AssetId` →
// `source_bytes` (encoded PNG/JPG, B.7.b.3 / ADR 0036). This module
// is the half that turns each asset's bytes into a sampled Vulkan
// texture so `ui::widget::image_overlay` can draw real pixels via
// ImGui's `AddImage` instead of the dashed placeholder.
//
// Ownership split:
//   - **Decode** lives in the caller (App). The caller hands us
//     decoded RGBA pixels via `upload(...)`. Keeping the decode out
//     of this module means a future async / off-thread decode can
//     drop in without changing this contract.
//   - **GPU resources** live here: one `gpu::Image` (VkImage +
//     VkImageView + VmaAllocation) per asset, the ImGui-side
//     descriptor set obtained from `ImGui_ImplVulkan_AddTexture`, and
//     a single shared `VkSampler` used by every asset.
//
// Frame-in-flight discipline:
//   - `upload(...)` and `remove(...)` mutate the live map immediately,
//     but old / removed entries enter a delayed-destruction queue and
//     are not freed until `kRetireDelayFrames` `tick()` calls later.
//     That covers the swapchain's `frames_in_flight = 2` plus a
//     one-frame margin, so any command buffer still referencing the
//     old descriptor set has retired by the time we release it.
//   - The bridge call (`sync_image_assets`) MUST run **before** the
//     frame's command buffer is recorded — pairs with App's existing
//     "imgui begin_frame happens BEFORE renderer.render_with_canvas"
//     ordering.
//
// What this module does NOT do:
//   - **No streaming decode policy.** Decode is synchronous on the
//     caller's thread; a 4K JPG can stall a frame for tens of ms.
//     Acceptable for v0.x; an async decoder is `feat/async-asset-
//     loading` work. See ADR 0037.
//   - **No mipmaps.** `LINEAR` min/mag filter on the single full-res
//     mip. Mipmaps land alongside the upload-quality pass.
//   - **No tinting at the texture level.** Per-primitive RGBA tint is
//     applied by the overlay via ImGui's `AddImage` `col` parameter
//     so a slider tweak doesn't re-upload pixels.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/domain/document/asset_id.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/image.hpp"

namespace noted::domain {
class ImageAssetRegistry;
}  // namespace noted::domain

namespace noted::compositor {

// Inputs for one upload. RGBA bytes must be tightly packed,
// `width * height * 4` bytes, top-left origin, 8 bpc.
struct ImageUploadInfo {
    noted::domain::AssetId id{noted::domain::invalid_asset_id};
    std::uint32_t width{0};
    std::uint32_t height{0};
    const std::byte* rgba_pixels{nullptr};
    std::size_t rgba_byte_count{0};
};

class ImageAssetGpuRegistry {
public:
    // Construct the registry with a long-lived device + allocator.
    // Builds the shared LINEAR sampler. The ImGui Vulkan backend must
    // already be initialised; we don't take a handle to it because
    // `ImGui_ImplVulkan_AddTexture` / `RemoveTexture` are
    // backend-singleton free functions.
    [[nodiscard]] static auto create(const noted::gpu::Device& device,
                                     const noted::gpu::Allocator& allocator)
        -> noted::Result<ImageAssetGpuRegistry>;

    ImageAssetGpuRegistry(ImageAssetGpuRegistry&& other) noexcept;
    auto operator=(ImageAssetGpuRegistry&& other) noexcept -> ImageAssetGpuRegistry&;
    ImageAssetGpuRegistry(const ImageAssetGpuRegistry&) = delete;
    auto operator=(const ImageAssetGpuRegistry&) -> ImageAssetGpuRegistry& = delete;
    ~ImageAssetGpuRegistry();

    // Upload a decoded RGBA8 image for `id`. Replaces an existing
    // entry if one exists (old one goes onto the retire queue).
    // Errors:
    //   - `invalid_argument` if width/height == 0 or byte count
    //     mismatches w*h*4.
    //   - `invalid_state` for any underlying Vulkan / VMA failure
    //     (full disk, out-of-memory, etc).
    [[nodiscard]] auto upload(const ImageUploadInfo& info) -> noted::Result<void>;

    // Drop an asset. Queues its GPU resources for delayed
    // destruction; subsequent `texture_for(id)` calls return
    // VK_NULL_HANDLE. Returns true if `id` was live, false otherwise.
    auto remove(noted::domain::AssetId id) -> bool;

    // Look up the ImGui-visible descriptor set for `id`. Returns
    // VK_NULL_HANDLE when no live entry exists — the caller (typically
    // `image_overlay`) should fall back to a placeholder draw.
    [[nodiscard]] auto texture_for(noted::domain::AssetId id) const noexcept -> VkDescriptorSet;

    // True if a live entry exists for `id`. Used by the reconcile
    // loop in `sync_image_assets` to decide whether a fresh upload
    // is needed.
    [[nodiscard]] auto has(noted::domain::AssetId id) const noexcept -> bool;

    // Number of live (non-retired) entries. Diagnostic.
    [[nodiscard]] auto size() const noexcept -> std::size_t { return live_.size(); }

    // Snapshot the AssetIds with live GPU entries. Used by the
    // reconcile bridge to find orphans (GPU entries whose domain
    // asset has been removed). Returns by value so the caller can
    // iterate without holding any reference into the registry.
    [[nodiscard]] auto live_ids() const -> std::vector<noted::domain::AssetId>;

    // Advance the frame counter and release any retired entries whose
    // delay has elapsed. Call once per frame, before any draw call
    // that might reference an asset's descriptor set.
    void tick();

    // Drop every live + retired entry immediately. Used at shutdown;
    // callers must ensure no in-flight command buffer still uses the
    // descriptor sets (typically `vkDeviceWaitIdle` first).
    void shutdown() noexcept;

    // Delay between `remove()` / `upload()`-replacement and the
    // actual VkImage / descriptor-set release. Sized to cover the
    // swapchain's `frames_in_flight = 2` plus one margin frame.
    static constexpr std::uint64_t kRetireDelayFrames = 3;

private:
    // `gpu::Image` is move-only with a private default ctor (factory
    // construction only); wrapping in `std::optional` lets `Entry`
    // be default-constructible so the unordered_map / retired-queue
    // value paths (designated init etc.) don't need the impossible
    // default Image. Live entries always have `image.has_value()`.
    struct Entry {
        std::optional<noted::gpu::Image> image;
        VkDescriptorSet imgui_ds{VK_NULL_HANDLE};
    };
    struct Retired {
        Entry entry;
        std::uint64_t release_after_tick{0};
    };

    ImageAssetGpuRegistry() = default;
    void release_entry_(Entry& e) noexcept;

    const noted::gpu::Device* device_{nullptr};
    const noted::gpu::Allocator* allocator_{nullptr};
    VkSampler sampler_{VK_NULL_HANDLE};
    std::unordered_map<noted::domain::AssetId, Entry> live_;
    std::vector<Retired> retired_;
    std::uint64_t frame_counter_{0};
};

// Reconcile a GPU registry against a domain registry: upload any
// asset present in domain (with non-empty `source_bytes`) that's
// missing from GPU, and remove any GPU entry whose asset is no
// longer in domain. Pure orchestration — does not call `tick()`.
//
// Decode goes through `platform::image_io::decode_rgba8`. Per-asset
// decode failures (corrupt bytes) log to stderr but do not halt the
// reconcile; the asset stays in the "no GPU texture" state and the
// overlay falls back to placeholder.
//
// Returns the first hard error from `upload()` if any; per-asset
// decode failures are not propagated. Successful syncs return
// `Result<void>{}`.
[[nodiscard]] auto sync_image_assets(ImageAssetGpuRegistry& gpu_registry,
                                     const noted::domain::ImageAssetRegistry& source)
    -> noted::Result<void>;

}  // namespace noted::compositor
