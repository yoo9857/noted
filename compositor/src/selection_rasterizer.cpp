#include "noted/compositor/selection_rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/selection_mask.hpp"
#include "noted/engine/profile.hpp"

namespace noted::compositor {

namespace {

[[nodiscard]] auto staging_size_bytes(VkExtent2D extent) noexcept -> VkDeviceSize {
    return static_cast<VkDeviceSize>(extent.width) * static_cast<VkDeviceSize>(extent.height) *
           noted::gpu::SelectionMask::bytes_per_texel();
}

// Scanline-fill a single polygon into `dest`. Standard odd-even
// horizontal scanline algorithm: for each scan-line `y` within the
// polygon's AABB (clipped to mask), gather the x-intersection of
// every edge that straddles `y`, sort, and fill between consecutive
// pairs.
//
// The edge straddling test uses the half-open convention
// `(yi <= y) != (yj <= y)` — matches `LassoPolygon::contains` so the
// rasterizer and the point-in-polygon predicate agree on the same
// boundary semantics. (Different conventions create one-pixel
// disagreements at horizontal edges.)
//
// `extent` is the clip rect; `dest` is the row-packed R8 mask. The
// function is noexcept and allocates a small scratch vector for the
// row's x-intersections — re-used across rows via the caller-
// supplied buffer.
void fill_polygon(const noted::domain::LassoPolygon& poly,
                  VkExtent2D extent,
                  std::span<std::uint8_t> dest,
                  std::uint8_t fill_value,
                  std::vector<double>& scratch_xs) noexcept {
    if (poly.is_empty()) {
        return;
    }
    const auto aabb = poly.aabb();
    if (!aabb) {
        return;
    }
    const auto& verts = poly.vertices();
    const std::int32_t mask_w = static_cast<std::int32_t>(extent.width);
    const std::int32_t mask_h = static_cast<std::int32_t>(extent.height);
    const auto y0 = std::max<std::int32_t>(aabb->y, 0);
    const auto y1 = std::min<std::int32_t>(aabb->bottom(), mask_h);
    if (y1 <= y0) {
        return;
    }

    const std::size_t stride = static_cast<std::size_t>(extent.width);
    for (std::int32_t y = y0; y < y1; ++y) {
        scratch_xs.clear();
        const std::size_t n = verts.size();
        std::size_t j = n - 1;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& vi = verts[i];
            const auto& vj = verts[j];
            // Half-open in y — vertices on the scanline are counted
            // once. Standard scanline-fill convention.
            const bool y_split = (vi.y <= y) != (vj.y <= y);
            if (y_split) {
                const double t = static_cast<double>(y - vi.y) / static_cast<double>(vj.y - vi.y);
                scratch_xs.push_back(static_cast<double>(vi.x) +
                                     t * static_cast<double>(vj.x - vi.x));
            }
            j = i;
        }
        if (scratch_xs.empty()) {
            continue;
        }
        std::sort(scratch_xs.begin(), scratch_xs.end());
        // Fill spans [xs[k], xs[k+1]) for each consecutive pair.
        // Round inwards (ceil left, floor right) so adjacent
        // polygons sharing an edge don't double-fill that column.
        for (std::size_t k = 0; k + 1 < scratch_xs.size(); k += 2) {
            const auto lx_raw = scratch_xs[k];
            const auto rx_raw = scratch_xs[k + 1];
            const std::int32_t lx =
                std::max<std::int32_t>(static_cast<std::int32_t>(std::ceil(lx_raw)), 0);
            const std::int32_t rx =
                std::min<std::int32_t>(static_cast<std::int32_t>(std::floor(rx_raw)) + 1, mask_w);
            if (rx <= lx) {
                continue;
            }
            auto* row =
                dest.data() + (static_cast<std::size_t>(y) * stride) + static_cast<std::size_t>(lx);
            std::memset(row, fill_value, static_cast<std::size_t>(rx - lx));
        }
    }
}

}  // namespace

void rasterize_to_buffer(const noted::domain::Selection& selection,
                         VkExtent2D extent,
                         std::span<std::uint8_t> dest,
                         std::uint8_t fill_value) noexcept {
    NOTED_PROFILE_ZONE_N("compositor::rasterize_to_buffer");
    if (extent.width == 0 || extent.height == 0) {
        return;
    }
    const auto expected_size =
        static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height);
    if (dest.size() != expected_size) {
        return;  // contract violation — silent, matches the rest of the codebase
    }

    const auto mask_w = static_cast<std::int32_t>(extent.width);
    const auto mask_h = static_cast<std::int32_t>(extent.height);

    for (const auto& r : selection.rects()) {
        // Clip the rect to mask bounds. The Selection canonical form
        // guarantees width > 0 && height > 0, but rect coordinates
        // may sit anywhere on the integer plane.
        const auto x0 = std::max<std::int32_t>(r.x, 0);
        const auto y0 = std::max<std::int32_t>(r.y, 0);
        const auto x1 = std::min<std::int32_t>(r.right(), mask_w);
        const auto y1 = std::min<std::int32_t>(r.bottom(), mask_h);
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }

        const auto stride = static_cast<std::size_t>(extent.width);
        const auto run = static_cast<std::size_t>(x1 - x0);
        for (std::int32_t y = y0; y < y1; ++y) {
            auto* row =
                dest.data() + (static_cast<std::size_t>(y) * stride) + static_cast<std::size_t>(x0);
            std::memset(row, fill_value, run);
        }
    }

    // Polygon scanline fill. Allocate the x-intersection scratch
    // buffer once and reuse across rows — avoids per-row malloc
    // churn in the (common) inner loop. The reservation upper-
    // bound is the maximum vertex count we expect; reserving a
    // small constant covers the realistic stroke-grade densities
    // and the vector grows transparently if a denser polygon
    // arrives.
    if (!selection.polygons().empty()) {
        std::vector<double> scratch_xs;
        scratch_xs.reserve(32);
        for (const auto& poly : selection.polygons()) {
            fill_polygon(poly, extent, dest, fill_value, scratch_xs);
        }
    }
}

void clear_and_rasterize(const noted::domain::Selection& selection,
                         VkExtent2D extent,
                         std::span<std::uint8_t> dest,
                         std::uint8_t fill_value) noexcept {
    if (!dest.empty()) {
        std::memset(dest.data(), 0, dest.size());
    }
    rasterize_to_buffer(selection, extent, dest, fill_value);
}

SelectionRasterizer::SelectionRasterizer(noted::gpu::Buffer staging, VkExtent2D capacity) noexcept
    : staging_{std::move(staging)}, capacity_{capacity} {}

auto SelectionRasterizer::create(const noted::gpu::Allocator& allocator,
                                 VkExtent2D extent) -> Result<SelectionRasterizer> {
    if (extent.width == 0 || extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SelectionRasterizer::create: extent has a zero dimension"));
    }
    auto staging = noted::gpu::Buffer::create(allocator,
                                              noted::gpu::BufferCreateInfo{
                                                  .size = staging_size_bytes(extent),
                                                  .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                  .memory = noted::gpu::MemoryUsage::cpu_to_gpu,
                                                  .persistent_map = true,
                                              });
    if (!staging) {
        return std::unexpected(std::move(staging).error());
    }
    return SelectionRasterizer{std::move(*staging), extent};
}

auto SelectionRasterizer::resize_staging(const noted::gpu::Allocator& allocator,
                                         VkExtent2D extent) -> Result<void> {
    if (extent.width == 0 || extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SelectionRasterizer::resize_staging: extent has a zero dimension"));
    }
    if (extent.width == capacity_.width && extent.height == capacity_.height) {
        return {};
    }
    auto fresh = noted::gpu::Buffer::create(allocator,
                                            noted::gpu::BufferCreateInfo{
                                                .size = staging_size_bytes(extent),
                                                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                .memory = noted::gpu::MemoryUsage::cpu_to_gpu,
                                                .persistent_map = true,
                                            });
    if (!fresh) {
        return std::unexpected(std::move(fresh).error());
    }
    staging_.emplace(std::move(*fresh));
    capacity_ = extent;
    return {};
}

auto SelectionRasterizer::record(VkCommandBuffer cb,
                                 const noted::domain::Selection& selection,
                                 noted::gpu::SelectionMask& mask) -> Result<void> {
    NOTED_PROFILE_ZONE_N("SelectionRasterizer::record");
    if (!staging_.has_value()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "SelectionRasterizer::record: staging disengaged"));
    }
    const auto mask_extent = mask.extent();
    if (mask_extent.width > capacity_.width || mask_extent.height > capacity_.height) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "SelectionRasterizer::record: mask exceeds staging capacity — call resize_staging() "
            "first"));
    }
    auto* mapped = staging_->mapped();
    if (mapped == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "SelectionRasterizer::record: staging not mapped"));
    }

    // 1. CPU rasterize into the mapped staging buffer. We use the
    //    mask's extent (not the staging capacity) so the upload region
    //    matches the destination image exactly.
    const auto pixel_count =
        static_cast<std::size_t>(mask_extent.width) * static_cast<std::size_t>(mask_extent.height);
    std::span<std::uint8_t> dest{static_cast<std::uint8_t*>(mapped), pixel_count};
    clear_and_rasterize(selection, mask_extent, dest);

    // 2. Transition mask → TRANSFER_DST.
    mask.transition_to(cb,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_COPY_BIT);

    // 3. Copy staging → mask. Tightly packed; matches the rasterizer.
    VkBufferImageCopy2 region{};
    region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mask_extent.width, mask_extent.height, 1U};

    VkCopyBufferToImageInfo2 copy{};
    copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
    copy.srcBuffer = staging_->handle();
    copy.dstImage = mask.handle();
    copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy.regionCount = 1;
    copy.pRegions = &region;
    vkCmdCopyBufferToImage2(cb, &copy);

    // 4. Transition mask → SHADER_READ_ONLY. Compositor/stroke engine
    //    sample from a fragment shader.
    mask.transition_to(cb,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    return {};
}

}  // namespace noted::compositor
