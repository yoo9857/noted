#include "noted/domain/tool/text_input.hpp"

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace {

using noted::domain::tool::text_primitive_from;
using noted::domain::tool::TextEditingState;
using noted::domain::tool::TextOptions;
using noted::domain::tool::TextPrimitive;

}  // namespace

// ---- text_primitive_from: identity / snapshot ------------------------------

TEST(TextPrimitiveFrom, CopiesPositionAndOptions) {
    TextOptions opt{};
    opt.font_size_px = 32.0F;
    opt.r = 0.5F;
    opt.g = 0.25F;
    opt.b = 0.125F;
    opt.a = 0.75F;

    const auto p = text_primitive_from(100.0, 200.0, "hello", opt);

    EXPECT_DOUBLE_EQ(p.x, 100.0);
    EXPECT_DOUBLE_EQ(p.y, 200.0);
    EXPECT_EQ(p.content, "hello");
    EXPECT_FLOAT_EQ(p.font_size_px, 32.0F);
    EXPECT_FLOAT_EQ(p.r, 0.5F);
    EXPECT_FLOAT_EQ(p.g, 0.25F);
    EXPECT_FLOAT_EQ(p.b, 0.125F);
    EXPECT_FLOAT_EQ(p.a, 0.75F);
}

TEST(TextPrimitiveFrom, EmptyContentRoundtripsToIsEmpty) {
    const auto p = text_primitive_from(0.0, 0.0, "", TextOptions{});
    EXPECT_TRUE(p.is_empty());
}

TEST(TextPrimitiveFrom, NonEmptyContentIsNotEmpty) {
    const auto p = text_primitive_from(0.0, 0.0, "x", TextOptions{});
    EXPECT_FALSE(p.is_empty());
}

// ---- text_primitive_from: trim --------------------------------------------

TEST(TextPrimitiveFromTrim, StripsLeadingAsciiWhitespace) {
    const auto p = text_primitive_from(0.0, 0.0, "   hello", TextOptions{});
    EXPECT_EQ(p.content, "hello");
}

TEST(TextPrimitiveFromTrim, StripsTrailingAsciiWhitespace) {
    const auto p = text_primitive_from(0.0, 0.0, "hello\t\n ", TextOptions{});
    EXPECT_EQ(p.content, "hello");
}

TEST(TextPrimitiveFromTrim, StripsBothSides) {
    const auto p = text_primitive_from(0.0, 0.0, " \r\nhello world \t", TextOptions{});
    EXPECT_EQ(p.content, "hello world");
}

TEST(TextPrimitiveFromTrim, AllWhitespaceCollapsesToEmpty) {
    const auto p = text_primitive_from(0.0, 0.0, "   \t\n\r ", TextOptions{});
    EXPECT_TRUE(p.is_empty());
}

TEST(TextPrimitiveFromTrim, PreservesUnicodeWhitespaceNbsp) {
    // U+00A0 NO-BREAK SPACE in UTF-8 = 0xC2 0xA0. ASCII trimmer must
    // leave it alone — a CJK user typing a fullwidth space meant it.
    const std::string nbsp{"\xC2\xA0"};
    const auto in = nbsp + "x" + nbsp;
    const auto p = text_primitive_from(0.0, 0.0, in, TextOptions{});
    EXPECT_EQ(p.content, in);
}

// ---- text_primitive_from: font-size clamp ---------------------------------

TEST(TextPrimitiveFromFontSize, ClampsZeroTo1px) {
    TextOptions opt{};
    opt.font_size_px = 0.0F;
    const auto p = text_primitive_from(0.0, 0.0, "x", opt);
    EXPECT_FLOAT_EQ(p.font_size_px, 1.0F);
}

TEST(TextPrimitiveFromFontSize, ClampsNegativeTo1px) {
    TextOptions opt{};
    opt.font_size_px = -42.0F;
    const auto p = text_primitive_from(0.0, 0.0, "x", opt);
    EXPECT_FLOAT_EQ(p.font_size_px, 1.0F);
}

TEST(TextPrimitiveFromFontSize, ClampsNanTo1px) {
    TextOptions opt{};
    opt.font_size_px = std::numeric_limits<float>::quiet_NaN();
    const auto p = text_primitive_from(0.0, 0.0, "x", opt);
    EXPECT_FLOAT_EQ(p.font_size_px, 1.0F);
}

TEST(TextPrimitiveFromFontSize, PassesThroughValidValues) {
    TextOptions opt{};
    opt.font_size_px = 1.0F;
    EXPECT_FLOAT_EQ(text_primitive_from(0.0, 0.0, "x", opt).font_size_px, 1.0F);
    opt.font_size_px = 16.5F;
    EXPECT_FLOAT_EQ(text_primitive_from(0.0, 0.0, "x", opt).font_size_px, 16.5F);
    opt.font_size_px = 999.0F;
    EXPECT_FLOAT_EQ(text_primitive_from(0.0, 0.0, "x", opt).font_size_px, 999.0F);
}

// ---- TextOptions equality (default-generated) -----------------------------

TEST(TextOptionsEquality, DefaultsCompareEqual) {
    EXPECT_EQ(TextOptions{}, TextOptions{});
}

TEST(TextOptionsEquality, FontSizeDifferenceCompares) {
    TextOptions a{};
    TextOptions b{};
    b.font_size_px = 24.0F;
    EXPECT_NE(a, b);
}

TEST(TextOptionsEquality, ColorDifferenceCompares) {
    TextOptions a{};
    TextOptions b{};
    b.r = 1.0F;
    EXPECT_NE(a, b);
}

// ---- TextEditingState defaults --------------------------------------------

TEST(TextEditingStateDefaults, ZeroPositionEmptyBufferDefaultOptions) {
    TextEditingState s{};
    EXPECT_DOUBLE_EQ(s.position_x, 0.0);
    EXPECT_DOUBLE_EQ(s.position_y, 0.0);
    EXPECT_EQ(s.buffer[0], '\0');
    EXPECT_EQ(s.options, TextOptions{});
    EXPECT_FALSE(s.needs_focus);
}

TEST(TextEditingStateDefaults, BufferCapacityIsKnown) {
    static_assert(TextEditingState::kBufferCapacity == 1024,
                  "Buffer capacity is wire-stable for v0.x — bump deliberately if changing.");
    EXPECT_EQ(TextEditingState::kBufferCapacity, std::size_t{1024});
}
