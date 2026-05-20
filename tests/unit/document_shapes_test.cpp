#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/tool/shape_drag.hpp"

namespace {

using noted::domain::Document;
using noted::domain::tool::ShapeKind;
using noted::domain::tool::ShapePrimitive;

[[nodiscard]] auto make_rect(double x0,
                             double y0,
                             double x1,
                             double y1) noexcept -> ShapePrimitive {
    ShapePrimitive p{};
    p.kind = ShapeKind::rectangle;
    p.x0 = x0;
    p.y0 = y0;
    p.x1 = x1;
    p.y1 = y1;
    p.stroke_width_px = 2.0F;
    p.stroke_a = 1.0F;
    return p;
}

}  // namespace

TEST(DocumentShapes, FreshDocumentHasEmptyShapes) {
    Document doc;
    EXPECT_TRUE(doc.shapes().empty());
    EXPECT_EQ(doc.shapes().size(), 0U);
}

TEST(DocumentShapes, AddShapeAssignsAppendIndex) {
    Document doc;
    auto a = doc.add_shape(make_rect(0.0, 0.0, 10.0, 10.0));
    auto b = doc.add_shape(make_rect(20.0, 20.0, 30.0, 30.0));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, 0U);
    EXPECT_EQ(*b, 1U);
    EXPECT_EQ(doc.shapes().size(), 2U);
}

TEST(DocumentShapes, AddShapePreservesAllFields) {
    Document doc;
    auto p = make_rect(5.5, 6.5, 15.5, 16.5);
    p.kind = ShapeKind::ellipse;
    p.stroke_r = 0.75F;
    p.stroke_width_px = 4.0F;
    (void) *doc.add_shape(p);
    const auto& got = doc.shapes()[0];
    EXPECT_EQ(got.kind, ShapeKind::ellipse);
    EXPECT_DOUBLE_EQ(got.x0, 5.5);
    EXPECT_DOUBLE_EQ(got.y1, 16.5);
    EXPECT_FLOAT_EQ(got.stroke_r, 0.75F);
    EXPECT_FLOAT_EQ(got.stroke_width_px, 4.0F);
}

TEST(DocumentShapes, RemoveShapeOutOfRangeRejects) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 10.0, 10.0));
    EXPECT_FALSE(doc.remove_shape(1));
    EXPECT_FALSE(doc.remove_shape(99));
}

TEST(DocumentShapes, RemoveShapeReflowsLowerShapes) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 1.0, 1.0));
    (void) *doc.add_shape(make_rect(2.0, 2.0, 3.0, 3.0));
    (void) *doc.add_shape(make_rect(4.0, 4.0, 5.0, 5.0));
    ASSERT_TRUE(doc.remove_shape(1));
    ASSERT_EQ(doc.shapes().size(), 2U);
    EXPECT_DOUBLE_EQ(doc.shapes()[0].x0, 0.0);
    EXPECT_DOUBLE_EQ(doc.shapes()[1].x0, 4.0);
}

TEST(DocumentShapes, InsertShapeAtSizeAppends) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 1.0, 1.0));
    auto idx = doc.insert_shape(1, make_rect(2.0, 2.0, 3.0, 3.0));
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
    EXPECT_EQ(doc.shapes().size(), 2U);
}

TEST(DocumentShapes, InsertShapeAtMiddleShifts) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 1.0, 1.0));
    (void) *doc.add_shape(make_rect(4.0, 4.0, 5.0, 5.0));
    auto idx = doc.insert_shape(1, make_rect(2.0, 2.0, 3.0, 3.0));
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
    EXPECT_DOUBLE_EQ(doc.shapes()[0].x0, 0.0);
    EXPECT_DOUBLE_EQ(doc.shapes()[1].x0, 2.0);
    EXPECT_DOUBLE_EQ(doc.shapes()[2].x0, 4.0);
}

TEST(DocumentShapes, InsertShapeOutOfRangeRejects) {
    Document doc;
    EXPECT_FALSE(doc.insert_shape(1, make_rect(0.0, 0.0, 1.0, 1.0)));
}

TEST(DocumentShapes, ReplaceShapesSwapsList) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 1.0, 1.0));
    std::vector<ShapePrimitive> repl;
    repl.push_back(make_rect(10.0, 10.0, 20.0, 20.0));
    repl.push_back(make_rect(30.0, 30.0, 40.0, 40.0));
    doc.replace_shapes(repl);
    ASSERT_EQ(doc.shapes().size(), 2U);
    EXPECT_DOUBLE_EQ(doc.shapes()[0].x0, 10.0);
}

TEST(DocumentShapes, ClearEmptiesShapes) {
    Document doc;
    (void) *doc.add_shape(make_rect(0.0, 0.0, 1.0, 1.0));
    doc.clear();
    EXPECT_TRUE(doc.shapes().empty());
}
