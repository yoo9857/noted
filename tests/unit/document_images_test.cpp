#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/tool/image_input.hpp"

namespace {

using noted::domain::Document;
using noted::domain::tool::ImagePrimitive;

[[nodiscard]] auto make_image(double x, double y, float w, float h) noexcept -> ImagePrimitive {
    ImagePrimitive p{};
    p.x = x;
    p.y = y;
    p.width_px = w;
    p.height_px = h;
    p.r = 1.0F;
    p.g = 1.0F;
    p.b = 1.0F;
    p.a = 1.0F;
    return p;
}

}  // namespace

TEST(DocumentImages, FreshDocumentHasEmptyImages) {
    Document doc;
    EXPECT_TRUE(doc.images().empty());
}

TEST(DocumentImages, AddImageAssignsAppendIndex) {
    Document doc;
    auto a = doc.add_image(make_image(0.0, 0.0, 100.0F, 100.0F));
    auto b = doc.add_image(make_image(150.0, 0.0, 200.0F, 200.0F));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, 0U);
    EXPECT_EQ(*b, 1U);
    EXPECT_EQ(doc.images().size(), 2U);
}

TEST(DocumentImages, AddImagePreservesAllFields) {
    Document doc;
    auto p = make_image(7.5, 8.5, 123.0F, 456.0F);
    p.r = 0.2F;
    p.g = 0.4F;
    p.b = 0.6F;
    p.a = 0.8F;
    (void) *doc.add_image(p);
    const auto& got = doc.images()[0];
    EXPECT_DOUBLE_EQ(got.x, 7.5);
    EXPECT_FLOAT_EQ(got.width_px, 123.0F);
    EXPECT_FLOAT_EQ(got.height_px, 456.0F);
    EXPECT_FLOAT_EQ(got.r, 0.2F);
    EXPECT_FLOAT_EQ(got.a, 0.8F);
}

TEST(DocumentImages, RemoveImageOutOfRangeRejects) {
    Document doc;
    EXPECT_FALSE(doc.remove_image(0));
}

TEST(DocumentImages, RemoveImageReflowsLowerImages) {
    Document doc;
    (void) *doc.add_image(make_image(0.0, 0.0, 10.0F, 10.0F));
    (void) *doc.add_image(make_image(20.0, 20.0, 10.0F, 10.0F));
    (void) *doc.add_image(make_image(40.0, 40.0, 10.0F, 10.0F));
    ASSERT_TRUE(doc.remove_image(1));
    ASSERT_EQ(doc.images().size(), 2U);
    EXPECT_DOUBLE_EQ(doc.images()[0].x, 0.0);
    EXPECT_DOUBLE_EQ(doc.images()[1].x, 40.0);
}

TEST(DocumentImages, InsertImageAtMiddleShifts) {
    Document doc;
    (void) *doc.add_image(make_image(0.0, 0.0, 10.0F, 10.0F));
    (void) *doc.add_image(make_image(40.0, 40.0, 10.0F, 10.0F));
    auto idx = doc.insert_image(1, make_image(20.0, 20.0, 10.0F, 10.0F));
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
    EXPECT_DOUBLE_EQ(doc.images()[1].x, 20.0);
}

TEST(DocumentImages, ReplaceImagesSwapsList) {
    Document doc;
    (void) *doc.add_image(make_image(0.0, 0.0, 10.0F, 10.0F));
    std::vector<ImagePrimitive> repl;
    repl.push_back(make_image(100.0, 100.0, 50.0F, 50.0F));
    doc.replace_images(repl);
    ASSERT_EQ(doc.images().size(), 1U);
    EXPECT_DOUBLE_EQ(doc.images()[0].x, 100.0);
}

TEST(DocumentImages, ClearEmptiesImages) {
    Document doc;
    (void) *doc.add_image(make_image(0.0, 0.0, 10.0F, 10.0F));
    doc.clear();
    EXPECT_TRUE(doc.images().empty());
}
