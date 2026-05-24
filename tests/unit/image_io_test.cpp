#include "noted/platform/image_io/image_io.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace {

using noted::platform::image_io::decode_rgba8;
using noted::platform::image_io::LoadedImage;

// ---- decode_rgba8 — rejection paths ----------------------------------------

TEST(ImageIoDecodeReject, EmptyBufferReturnsError) {
    auto r = decode_rgba8({});
    EXPECT_FALSE(r);
}

TEST(ImageIoDecodeReject, RandomBytesReturnError) {
    // A 16-byte buffer of arbitrary bytes can't possibly be a valid
    // image; stb_image rejects it and we surface the failure as an
    // `invalid_image_format` Result error.
    const std::array<std::byte, 16> junk{std::byte{0x01},
                                         std::byte{0x02},
                                         std::byte{0x03},
                                         std::byte{0x04},
                                         std::byte{0x05},
                                         std::byte{0x06},
                                         std::byte{0x07},
                                         std::byte{0x08},
                                         std::byte{0x09},
                                         std::byte{0x0A},
                                         std::byte{0x0B},
                                         std::byte{0x0C},
                                         std::byte{0x0D},
                                         std::byte{0x0E},
                                         std::byte{0x0F},
                                         std::byte{0x10}};
    auto r = decode_rgba8(std::span<const std::byte>{junk});
    EXPECT_FALSE(r);
}

// ---- decode_rgba8 — happy path via a hand-built 1×1 24-bit BMP ------------

// A 1×1 BGR(255,0,0) BMP file in BITMAPINFOHEADER form. 58 bytes
// total; well within `stbi_load_from_memory`'s tolerance window.
// Constructed by hand so the test has no external file dependency
// and compiles into the test binary without a fixture-loading step.
//
// Layout:
//   bytes  0..13  — BITMAPFILEHEADER (14 bytes): 'B','M' + file size
//                   + reserved + pixel-data offset (54).
//   bytes 14..53  — BITMAPINFOHEADER (40 bytes): width=1, height=1,
//                   planes=1, bpp=24, BI_RGB compression.
//   bytes 54..57  — pixel data: B=0xFF, G=0x00, R=0x00 + 1 byte
//                   padding to 4-byte row boundary.
//
// stb_image decodes BMP without any external deps, so this test
// proves the in-memory decode path is wired correctly without
// needing a real PNG / JPG fixture.
// clang-format off
constexpr std::array<std::uint8_t, 58> kTinyRedBmp{
    // BITMAPFILEHEADER
    0x42, 0x4D,                                 // 'B' 'M'
    0x3A, 0x00, 0x00, 0x00,                     // file size = 58
    0x00, 0x00, 0x00, 0x00,                     // reserved
    0x36, 0x00, 0x00, 0x00,                     // pixel offset = 54
    // BITMAPINFOHEADER
    0x28, 0x00, 0x00, 0x00,                     // header size = 40
    0x01, 0x00, 0x00, 0x00,                     // width = 1
    0x01, 0x00, 0x00, 0x00,                     // height = 1
    0x01, 0x00,                                 // planes = 1
    0x18, 0x00,                                 // bpp = 24
    0x00, 0x00, 0x00, 0x00,                     // compression = BI_RGB
    0x00, 0x00, 0x00, 0x00,                     // image size (0 = ignored for BI_RGB)
    0x00, 0x00, 0x00, 0x00,                     // x_ppm
    0x00, 0x00, 0x00, 0x00,                     // y_ppm
    0x00, 0x00, 0x00, 0x00,                     // colors used
    0x00, 0x00, 0x00, 0x00,                     // colors important
    // pixel data — single BGR pixel + 1 byte row padding
    0xFF, 0x00, 0x00, 0x00,
};
// clang-format on

[[nodiscard]] auto tiny_red_bmp_span() noexcept -> std::span<const std::byte> {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(kTinyRedBmp.data()),
                                      kTinyRedBmp.size()};
}

TEST(ImageIoDecodeBmp, TinyRedBmpDecodesToOnePixel) {
    auto r = decode_rgba8(tiny_red_bmp_span());
    ASSERT_TRUE(r) << (r ? "" : r.error().format());
    EXPECT_EQ(r->width, 1U);
    EXPECT_EQ(r->height, 1U);
    ASSERT_EQ(r->pixels.size(), 4U);
}

TEST(ImageIoDecodeBmp, PixelChannelsRedOpaque) {
    auto r = decode_rgba8(tiny_red_bmp_span());
    ASSERT_TRUE(r);
    // stb_image expands BGR → RGBA, so the byte order on output is
    // R, G, B, A. The BMP encoded blue=0xFF green=0 red=0; after
    // stb_image's BGR-to-RGB swap, R=0, G=0, B=0xFF. Alpha is 0xFF
    // (opaque) because stb_image promotes 3-channel inputs to RGBA
    // with full alpha.
    EXPECT_EQ(std::to_integer<int>(r->pixels[0]), 0x00);  // R
    EXPECT_EQ(std::to_integer<int>(r->pixels[1]), 0x00);  // G
    EXPECT_EQ(std::to_integer<int>(r->pixels[2]), 0xFF);  // B
    EXPECT_EQ(std::to_integer<int>(r->pixels[3]), 0xFF);  // A (opaque)
}

}  // namespace
