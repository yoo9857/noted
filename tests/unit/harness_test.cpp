#include <gtest/gtest.h>

#include <atomic>

#include "harness.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"

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

TEST(Harness, ScopedTimerPublishesSpan) {
    std::atomic<int> spans{0};
    double captured_ms = -1.0;
    const auto t = noted::hook::registry().on_timer_span.subscribe(
        [&](const noted::hook::TimerSpan& s) {
            if (s.label == "test.scoped_timer") {
                spans.fetch_add(1);
                captured_ms = s.ms;
            }
        });
    {
        NOTED_TIMED("test.scoped_timer");
    }
    EXPECT_EQ(spans.load(), 1);
    EXPECT_GE(captured_ms, 0.0);
    noted::hook::registry().on_timer_span.unsubscribe(t);
}

TEST(Harness, FlagChangePublishesEvent) {
    static noted::harness::FeatureFlag flag{"test.observed_flag", false};
    flag.set(false);  // baseline

    std::atomic<int> events{0};
    bool last_value = false;
    const auto t = noted::hook::registry().on_flag_changed.subscribe(
        [&](const noted::hook::FlagChanged& e) {
            if (e.name == "test.observed_flag") {
                events.fetch_add(1);
                last_value = e.new_value;
            }
        });

    flag.set(true);
    flag.set(true);   // same value: no event
    flag.set(false);

    EXPECT_EQ(events.load(), 2);
    EXPECT_FALSE(last_value);
    noted::hook::registry().on_flag_changed.unsubscribe(t);
}

TEST(Harness, ResetAllRestoresDefaults) {
    static noted::harness::FeatureFlag flag{"test.reset_flag", true};
    static noted::harness::Counter ctr{"test.reset_counter"};

    flag.set(false);
    ctr.add(100);
    ASSERT_FALSE(static_cast<bool>(flag));
    ASSERT_EQ(ctr.value(), 100U);

    noted::harness::reset_all();

    EXPECT_TRUE(static_cast<bool>(flag));
    EXPECT_EQ(ctr.value(), 0U);
}
