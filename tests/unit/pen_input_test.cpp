#include "noted/platform/window/pen_input.hpp"

#include <gtest/gtest.h>

#include "noted/engine/hook/hook.hpp"

using noted::platform::pen::normalize;
using noted::platform::pen::RawPenSample;

TEST(PenInput, NormalizePressureZero) {
    const auto e = normalize(RawPenSample{
        .client_x = 0.0,
        .client_y = 0.0,
        .pressure_0_1024 = 0,
        .tilt_x_deg = 0,
        .tilt_y_deg = 0,
    });
    EXPECT_FLOAT_EQ(e.pressure, 0.0F);
    EXPECT_FLOAT_EQ(e.tilt_x, 0.0F);
    EXPECT_FLOAT_EQ(e.tilt_y, 0.0F);
}

TEST(PenInput, NormalizePressureFullScale) {
    const auto e = normalize(RawPenSample{.pressure_0_1024 = 1024});
    EXPECT_FLOAT_EQ(e.pressure, 1.0F);
}

TEST(PenInput, NormalizePressureMidRange) {
    // 512 / 1024 = 0.5 exactly.
    const auto e = normalize(RawPenSample{.pressure_0_1024 = 512});
    EXPECT_FLOAT_EQ(e.pressure, 0.5F);
}

TEST(PenInput, NormalizePressureClampsOverflow) {
    // A handful of drivers report >1024 at the top of the press range.
    // The translation must never propagate >1.0 — downstream consumers
    // multiply alpha / radius by pressure and an overflow would clip.
    const auto e = normalize(RawPenSample{.pressure_0_1024 = 2048});
    EXPECT_FLOAT_EQ(e.pressure, 1.0F);
}

TEST(PenInput, NormalizePassesCoordsThrough) {
    const auto e = normalize(RawPenSample{
        .client_x = 123.45,
        .client_y = 678.90,
    });
    EXPECT_DOUBLE_EQ(e.x, 123.45);
    EXPECT_DOUBLE_EQ(e.y, 678.90);
}

TEST(PenInput, NormalizeTiltForwarded) {
    // Tilt range per Windows POINTER_PEN_INFO docs: -90..+90 degrees.
    const auto neg = normalize(RawPenSample{
        .tilt_x_deg = -45,
        .tilt_y_deg = -30,
    });
    EXPECT_FLOAT_EQ(neg.tilt_x, -45.0F);
    EXPECT_FLOAT_EQ(neg.tilt_y, -30.0F);

    const auto pos = normalize(RawPenSample{
        .tilt_x_deg = 75,
        .tilt_y_deg = 60,
    });
    EXPECT_FLOAT_EQ(pos.tilt_x, 75.0F);
    EXPECT_FLOAT_EQ(pos.tilt_y, 60.0F);
}

TEST(PenInput, NormalizeOldHardwareMaxesAtLowerCap) {
    // Older Wacom tablets that report 0..511. They never report 1024,
    // so a value of 511 means "max pressure for this device". We
    // intentionally divide by the full scale so users with such hardware
    // get ~0.5 — accepting the dynamic-range loss in exchange for
    // cross-device consistency.
    const auto e = normalize(RawPenSample{.pressure_0_1024 = 511});
    EXPECT_NEAR(e.pressure, 0.499F, 0.001F);
}
