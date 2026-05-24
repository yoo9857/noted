// Single implementation TU for stb_image across the whole project.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC

#include "noted/platform/image_io/image_io.hpp"

#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include <stb_image.h>

namespace noted::platform::image_io {

namespace {

// Shared post-decode path: stb_image returns a malloc'd RGBA buffer +
// width/height; copy into our owning vector, free the source, return.
// Both load_rgba8 (file) and decode_rgba8 (memory) reach this point
// after they've successfully decoded their respective inputs.
[[nodiscard]] auto finalize(stbi_uc* raw, int w, int h) -> LoadedImage {
    LoadedImage out;
    out.width = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    const auto byte_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.pixels.resize(byte_count);
    std::memcpy(out.pixels.data(), raw, byte_count);
    stbi_image_free(raw);
    return out;
}

}  // namespace

auto load_rgba8(const std::filesystem::path& p) -> noted::Result<LoadedImage> {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* raw = stbi_load(p.string().c_str(), &w, &h, &comp, /*req_comp=*/4);
    if (raw == nullptr) {
        const char* reason = stbi_failure_reason();
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": " + (reason != nullptr ? reason : "stb_image failed"),
            .where = std::source_location::current(),
        });
    }
    return finalize(raw, w, h);
}

auto decode_rgba8(std::span<const std::byte> bytes) -> noted::Result<LoadedImage> {
    if (bytes.empty()) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_argument,
            .message = "decode_rgba8: empty byte span",
            .where = std::source_location::current(),
        });
    }
    int w = 0;
    int h = 0;
    int comp = 0;
    // stbi_load_from_memory takes signed-int byte counts. The 64 MiB
    // per-asset cap that platform::io::load_noted_file enforces is well
    // below INT_MAX, but guard explicitly so the cast is provably safe
    // even if the byte span originated outside the .noted load path.
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_argument,
            .message = "decode_rgba8: byte span exceeds INT_MAX",
            .where = std::source_location::current(),
        });
    }
    stbi_uc* raw = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                         static_cast<int>(bytes.size()),
                                         &w,
                                         &h,
                                         &comp,
                                         /*req_comp=*/4);
    if (raw == nullptr) {
        const char* reason = stbi_failure_reason();
        return std::unexpected(noted::Error{
            .code = noted::ErrorCode::invalid_image_format,
            .message =
                std::string{"decode_rgba8: "} + (reason != nullptr ? reason : "stb_image failed"),
            .where = std::source_location::current(),
        });
    }
    return finalize(raw, w, h);
}

}  // namespace noted::platform::image_io
