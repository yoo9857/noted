#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
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

// Same decode, in-memory variant: parses an encoded image (PNG / JPG /
// BMP / TGA) directly from `bytes` without touching the filesystem.
// Used by the asset GPU registry to re-decode the `source_bytes`
// payload that the .noted zip carries (B.7.b.3 / ADR 0036) so a
// loaded file can paint real images without re-picking from disk.
//
// Errors with `invalid_argument` on an empty buffer and
// `invalid_image_format` when stb_image rejects the contents. Same
// post-condition as `load_rgba8`: 4 channels, RGBA, 8 bpc, top-left
// origin, `.pixels.size() == width * height * 4`.
[[nodiscard]] auto decode_rgba8(std::span<const std::byte> bytes) -> noted::Result<LoadedImage>;

}  // namespace noted::platform::image_io
