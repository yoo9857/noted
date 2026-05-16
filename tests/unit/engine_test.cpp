#include "noted/engine/engine.hpp"

#include <atomic>

#include <gtest/gtest.h>

#include "noted/engine/hook/registry.hpp"

#include "harness.hpp"

TEST(Engine, InitShutdownIsIdempotent) {
    noted::engine::Engine eng;
    ASSERT_TRUE(eng.init().has_value());
    EXPECT_TRUE(eng.is_initialized());
    ASSERT_TRUE(eng.init().has_value());  // second init is a no-op
    ASSERT_TRUE(eng.shutdown().has_value());
    EXPECT_FALSE(eng.is_initialized());
    ASSERT_TRUE(eng.shutdown().has_value());  // second shutdown is a no-op
}

TEST(Engine, StartupAndShutdownHooksFire) {
    std::atomic<int> startups{0};
    std::atomic<int> shutdowns{0};
    const auto t1 = noted::hook::registry().on_startup.subscribe(
        [&](const noted::hook::EngineStartup&) { startups.fetch_add(1); });
    const auto t2 = noted::hook::registry().on_shutdown.subscribe(
        [&](const noted::hook::EngineShutdown&) { shutdowns.fetch_add(1); });

    {
        noted::engine::Engine eng;
        ASSERT_TRUE(eng.init().has_value());
        ASSERT_TRUE(eng.shutdown().has_value());
    }

    EXPECT_EQ(startups.load(), 1);
    EXPECT_EQ(shutdowns.load(), 1);
    noted::hook::registry().on_startup.unsubscribe(t1);
    noted::hook::registry().on_shutdown.unsubscribe(t2);
}

TEST(Engine, FrameLifecyclePublishesHooks) {
    std::atomic<int> begins{0};
    std::atomic<int> ends{0};
    std::uint64_t last_end_index = 0;
    const auto t1 = noted::hook::registry().on_frame_begin.subscribe(
        [&](const noted::hook::FrameBegin&) { begins.fetch_add(1); });
    const auto t2 =
        noted::hook::registry().on_frame_end.subscribe([&](const noted::hook::FrameEnd& f) {
            ends.fetch_add(1);
            last_end_index = f.frame_index;
        });

    noted::engine::Engine eng;
    ASSERT_TRUE(eng.init().has_value());
    for (int i = 0; i < 5; ++i) {
        eng.begin_frame();
        eng.end_frame();
    }
    ASSERT_TRUE(eng.shutdown().has_value());

    EXPECT_EQ(begins.load(), 5);
    EXPECT_EQ(ends.load(), 5);
    EXPECT_EQ(last_end_index, 4U);     // 0-indexed final frame
    EXPECT_EQ(eng.frame_index(), 5U);  // post-increment final state

    noted::hook::registry().on_frame_begin.unsubscribe(t1);
    noted::hook::registry().on_frame_end.unsubscribe(t2);
}

TEST(Engine, BeginFrameBeforeInitFlagsError) {
    noted::test::ErrorCapture cap;
    noted::engine::Engine eng;
    eng.begin_frame();  // misuse: no init
    EXPECT_GE(cap.errors().size(), 1U);
}

TEST(Engine, DestructorRunsShutdownIfInitialized) {
    std::atomic<int> shutdowns{0};
    const auto t = noted::hook::registry().on_shutdown.subscribe(
        [&](const noted::hook::EngineShutdown&) { shutdowns.fetch_add(1); });
    {
        noted::engine::Engine eng;
        ASSERT_TRUE(eng.init().has_value());
        // intentionally no manual shutdown — destructor must clean up.
    }
    EXPECT_EQ(shutdowns.load(), 1);
    noted::hook::registry().on_shutdown.unsubscribe(t);
}
