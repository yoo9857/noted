#include "noted/domain/tool/tool.hpp"

#include <gtest/gtest.h>

namespace {

using noted::domain::tool::kFirstTool;
using noted::domain::tool::kLastTool;
using noted::domain::tool::kToolCount;
using noted::domain::tool::label;
using noted::domain::tool::ToolKind;
using noted::domain::tool::ToolState;

}  // namespace

TEST(ToolState, DefaultsToPen) {
    ToolState s;
    EXPECT_EQ(s.active, ToolKind::pen);
}

TEST(ToolState, EqualityFollowsActive) {
    ToolState a;
    ToolState b;
    EXPECT_EQ(a, b);
    b.active = ToolKind::eraser;
    EXPECT_NE(a, b);
}

// Wire-stable ordinals — `.noted` v3 will persist `active`. Pinned so
// a reorder doesn't slip through review.
TEST(ToolKindOrdinals, MatchSchemaContract) {
    EXPECT_EQ(static_cast<int>(ToolKind::pen), 0);
    EXPECT_EQ(static_cast<int>(ToolKind::eraser), 1);
    EXPECT_EQ(static_cast<int>(ToolKind::select), 2);
    EXPECT_EQ(static_cast<int>(ToolKind::shape), 3);
    EXPECT_EQ(static_cast<int>(ToolKind::text), 4);
    EXPECT_EQ(static_cast<int>(ToolKind::image), 5);
}

TEST(ToolKindBounds, FirstAndLastMatchCount) {
    EXPECT_EQ(kFirstTool, ToolKind::pen);
    EXPECT_EQ(kLastTool, ToolKind::image);
    EXPECT_EQ(kToolCount, 6U);
    // kFirstTool..kLastTool spans exactly kToolCount tools.
    EXPECT_EQ(
        static_cast<std::size_t>(static_cast<int>(kLastTool) - static_cast<int>(kFirstTool) + 1),
        kToolCount);
}

TEST(ToolLabel, NonEmptyForEveryTool) {
    EXPECT_EQ(label(ToolKind::pen), "Pen");
    EXPECT_EQ(label(ToolKind::eraser), "Eraser");
    EXPECT_EQ(label(ToolKind::select), "Select");
    EXPECT_EQ(label(ToolKind::shape), "Shape");
    EXPECT_EQ(label(ToolKind::text), "Text");
    EXPECT_EQ(label(ToolKind::image), "Image");
}
