#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include "noted/engine/hook/hook.hpp"

namespace {

struct Ping {
    int payload = 0;
};

}  // namespace

TEST(Hook, SubscribePublishUnsubscribe) {
    noted::hook::Channel<Ping> ch;
    std::atomic<int> seen{0};
    const auto t = ch.subscribe([&](const Ping& p) { seen += p.payload; });
    ch.publish(Ping{.payload = 5});
    ch.publish(Ping{.payload = 7});
    EXPECT_EQ(seen.load(), 12);
    ch.unsubscribe(t);
    ch.publish(Ping{.payload = 1});
    EXPECT_EQ(seen.load(), 12);
}

TEST(Hook, PriorityOrderingLowFirst) {
    noted::hook::Channel<Ping> ch;
    std::vector<int> order;
    ch.subscribe([&](const Ping&) { order.push_back(2); }, /*priority=*/10);
    ch.subscribe([&](const Ping&) { order.push_back(1); }, /*priority=*/0);
    ch.subscribe([&](const Ping&) { order.push_back(3); }, /*priority=*/20);
    ch.publish({});
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(Hook, DeferredPublishWaitsForFlush) {
    noted::hook::Channel<Ping> ch;
    int n = 0;
    ch.subscribe([&](const Ping&) { ++n; });
    ch.publish_deferred(Ping{});
    ch.publish_deferred(Ping{});
    EXPECT_EQ(n, 0);
    ch.flush();
    EXPECT_EQ(n, 2);
}

TEST(Hook, RaiiSubscriptionAutoUnsubscribes) {
    noted::hook::Channel<Ping> ch;
    int n = 0;
    {
        noted::hook::Subscription<Ping> sub(
            ch, ch.subscribe([&](const Ping&) { ++n; }));
        ch.publish({});
    }
    ch.publish({});
    EXPECT_EQ(n, 1);
}
