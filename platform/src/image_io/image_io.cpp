// Single implementation TU for stb_image across the whole project.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC

#include "noted/platform/image_io/image_io.hpp"

#include <cstring>
#include <utility>

#include <stb_image.h>

namespace noted::platform::image_io {

auto load_rgba8(const std::filesystem::path& p) -> noted::Result<LoadedImage> {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* raw = stbi_load(p.string().c_str(), &w, &h, &comp, /*req_comp=*/4);
    if (raw == nullptr) {
        const char* reason = stbi_failure_reason();
        return std::unexpected(noted::Error{
            .code    = noted::ErrorCode::invalid_image_format,
            .message = p.string() + ": " + (reason != nullptr ? reason : "stb_image failed"),
            .where   = std::source_location::current(),
        });
    }
    LoadedImage out;
    out.width  = static_cast<std::uint32_t>(w);
    out.height = static_cast<std::uint32_t>(h);
    const auto byte_count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
    out.pixels.resize(byte_count);
    std::memcpy(out.pixels.data(), raw, byte_count);
    stbi_image_free(raw);
    return out;
}

}  // namespace noted::platform::image_io
