#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::platform::image_io {

struct LoadedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Always 4 channels, RGBA, 8 bits per channel, top-left origin.
    std::vector<std::byte> pixels;
};

// Decode a file from disk (PNG / JPG / BMP / TGA via stb_image) into a
// tightly-packed 8-bit RGBA buffer.
//
// On success, .pixels.size() == width * height * 4. On failure, returns
// invalid_image_format / file_not_found with the stb_image reason in
// the message.
[[nodiscard]] auto load_rgba8(const std::filesystem::path& p) -> noted::Result<LoadedImage>;

}  // namespace noted::platform::image_io
