#include "noted/domain/layer/canvas_layer_stack.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace {

using noted::domain::CanvasLayer;
using noted::domain::CanvasLayerStack;

TEST(CanvasLayerStack, FreshStackIsEmpty) {
    CanvasLayerStack stack;
    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.size(), 0U);
    EXPECT_EQ(stack.find(1), nullptr);
}

TEST(CanvasLayerStack, AddLayerAllocatesMonotonicIds) {
    CanvasLayerStack stack;
    auto a = stack.add_layer("Layer 1");
    auto b = stack.add_layer("Layer 2");
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_NE(*a, *b);
    EXPECT_GT(*b, *a);  // monotonic
    EXPECT_EQ(stack.size(), 2U);
    ASSERT_NE(stack.find(*a), nullptr);
    EXPECT_EQ(stack.find(*a)->name, "Layer 1");
    EXPECT_TRUE(stack.find(*a)->visible);
    EXPECT_FLOAT_EQ(stack.find(*a)->opacity, 1.0F);
}

TEST(CanvasLayerStack, RemoveLayerPullsByIndex) {
    CanvasLayerStack stack;
    auto a = stack.add_layer("Layer 1");
    auto b = stack.add_layer("Layer 2");
    auto c = stack.add_layer("Layer 3");
    ASSERT_TRUE(a && b && c);
    auto removed = stack.remove_layer(1);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->id, *b);
    EXPECT_EQ(stack.size(), 2U);
    EXPECT_EQ(stack.find(*b), nullptr);
    EXPECT_NE(stack.find(*a), nullptr);
    EXPECT_NE(stack.find(*c), nullptr);
}

TEST(CanvasLayerStack, RemoveOutOfRangeRejects) {
    CanvasLayerStack stack;
    EXPECT_FALSE(stack.remove_layer(0));
    (void) *stack.add_layer("L");
    EXPECT_FALSE(stack.remove_layer(1));
}

TEST(CanvasLayerStack, SetVisibleTogglesAndAffectsIsVisible) {
    CanvasLayerStack stack;
    auto a = *stack.add_layer("L");
    EXPECT_TRUE(stack.is_visible(a));
    ASSERT_TRUE(stack.set_visible(a, false));
    EXPECT_FALSE(stack.is_visible(a));
    EXPECT_FALSE(stack.find(a)->visible);
    ASSERT_TRUE(stack.set_visible(a, true));
    EXPECT_TRUE(stack.is_visible(a));
}

TEST(CanvasLayerStack, IsVisibleUnknownIdIsFalse) {
    CanvasLayerStack stack;
    // Unknown id (including the reserved 0) must return false so the
    // renderer treats orphaned strokes as hidden rather than crashing.
    EXPECT_FALSE(stack.is_visible(0));
    EXPECT_FALSE(stack.is_visible(9999));
}

TEST(CanvasLayerStack, SetVisibleUnknownRejects) {
    CanvasLayerStack stack;
    EXPECT_FALSE(stack.set_visible(42, true));
}

TEST(CanvasLayerStack, SetOpacityClampsNanAndOutOfRange) {
    CanvasLayerStack stack;
    auto a = *stack.add_layer("L");
    ASSERT_TRUE(stack.set_opacity(a, -0.5F));
    EXPECT_FLOAT_EQ(stack.find(a)->opacity, 0.0F);
    ASSERT_TRUE(stack.set_opacity(a, 2.0F));
    EXPECT_FLOAT_EQ(stack.find(a)->opacity, 1.0F);
    ASSERT_TRUE(stack.set_opacity(a, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FLOAT_EQ(stack.find(a)->opacity, 0.0F);
}

TEST(CanvasLayerStack, SetNameUpdatesEntry) {
    CanvasLayerStack stack;
    auto a = *stack.add_layer("Old");
    ASSERT_TRUE(stack.set_name(a, "New"));
    EXPECT_EQ(stack.find(a)->name, "New");
}

TEST(CanvasLayerStack, MoveReordersList) {
    CanvasLayerStack stack;
    auto a = *stack.add_layer("A");
    auto b = *stack.add_layer("B");
    auto c = *stack.add_layer("C");
    // Initial order [A, B, C]; move(2, 0) → [C, A, B]
    ASSERT_TRUE(stack.move(2, 0));
    EXPECT_EQ(stack.layers()[0].id, c);
    EXPECT_EQ(stack.layers()[1].id, a);
    EXPECT_EQ(stack.layers()[2].id, b);
}

TEST(CanvasLayerStack, MoveOutOfRangeRejects) {
    CanvasLayerStack stack;
    (void) *stack.add_layer("A");
    EXPECT_FALSE(stack.move(0, 5));
    EXPECT_FALSE(stack.move(5, 0));
}

TEST(CanvasLayerStack, InsertLayerRequiresNonZeroId) {
    CanvasLayerStack stack;
    CanvasLayer l{};
    l.id = noted::invalid_layer_id;  // 0 — invalid
    l.name = "Bad";
    EXPECT_FALSE(stack.insert_layer(0, l));
}

TEST(CanvasLayerStack, InsertLayerRejectsDuplicateId) {
    CanvasLayerStack stack;
    CanvasLayer l1{};
    l1.id = 42;
    l1.name = "A";
    CanvasLayer l2{};
    l2.id = 42;
    l2.name = "B";
    ASSERT_TRUE(stack.insert_layer(0, l1));
    EXPECT_FALSE(stack.insert_layer(0, l2));
}

TEST(CanvasLayerStack, InsertLayerBumpsNextIdAllocator) {
    CanvasLayerStack stack;
    CanvasLayer l{};
    l.id = 100;
    l.name = "Hundred";
    ASSERT_TRUE(stack.insert_layer(0, l));
    // Subsequent add_layer must allocate > 100 to avoid id collision.
    auto next = stack.add_layer("Next");
    ASSERT_TRUE(next);
    EXPECT_GT(*next, 100);
}

TEST(CanvasLayerStack, ReplaceClampsOpacities) {
    CanvasLayerStack stack;
    std::vector<CanvasLayer> layers;
    CanvasLayer l1{};
    l1.id = 7;
    l1.name = "Out-of-range";
    l1.opacity = 5.0F;
    layers.push_back(l1);
    CanvasLayer l2{};
    l2.id = 9;
    l2.name = "NaN";
    l2.opacity = std::numeric_limits<float>::quiet_NaN();
    layers.push_back(l2);
    stack.replace(std::move(layers));
    EXPECT_FLOAT_EQ(stack.find(7)->opacity, 1.0F);
    EXPECT_FLOAT_EQ(stack.find(9)->opacity, 0.0F);
    // Next allocation must avoid 7 and 9.
    auto next = stack.add_layer("Next");
    ASSERT_TRUE(next);
    EXPECT_GT(*next, 9);
}

}  // namespace
