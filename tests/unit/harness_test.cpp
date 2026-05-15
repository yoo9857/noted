#include <gtest/gtest.h>

#include "harness.hpp"
#include "noted/engine/harness/harness.hpp"

TEST(Harness, FeatureFlagDefaultThenToggle) {
    static noted::harness::FeatureFlag flag{"test.demo_flag", false};
    EXPECT_FALSE(static_cast<bool>(flag));
    flag.set(true);
    EXPECT_TRUE(static_cast<bool>(flag));
    flag.set(false);
}

TEST(Harness, FlagDiscoverableByName) {
    static noted::harness::FeatureFlag flag{"test.lookup_flag", true};
    auto* found = noted::harness::find_flag("test.lookup_flag");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(static_cast<bool>(*found));
    EXPECT_EQ(found, &flag);
}

TEST(Harness, CounterAtomicIncrement) {
    static noted::harness::Counter ctr{"test.counter"};
    ctr.reset();
    ctr.add(3);
    ctr.add(4);
    EXPECT_EQ(ctr.value(), 7U);
}

TEST(Harness, ValidateEmitsErrorEvent) {
    noted::test::ErrorCapture cap;
    noted::harness::validate(false, "boom");
    ASSERT_EQ(cap.errors().size(), 1U);
    EXPECT_EQ(cap.errors()[0].code, noted::ErrorCode::invalid_state);
}

TEST(Harness, ConfigRoundTrip) {
    auto& cfg = noted::harness::Config::instance();
    cfg.set_int("tile.size", 256);
    cfg.set_bool("gpu.validation", true);
    cfg.set_string("ui.theme", "dark");
    EXPECT_EQ(cfg.get_int("tile.size"), 256);
    EXPECT_TRUE(cfg.get_bool("gpu.validation"));
    EXPECT_EQ(cfg.get_string("ui.theme"), "dark");
    EXPECT_EQ(cfg.get_int("does.not.exist", 42), 42);
}
