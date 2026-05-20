#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"
#include "noted/engine/canvas/page.hpp"

namespace {

using noted::canvas::Page;
using noted::canvas::PageBackground;
using noted::canvas::PageList;
using noted::domain::Document;

}  // namespace

TEST(DocumentPages, FreshDocumentHasEmptyPages) {
    Document doc;
    EXPECT_TRUE(doc.pages().empty());
    EXPECT_EQ(doc.pages().size(), 0U);
}

TEST(DocumentPages, AddPageAssignsAppendIndex) {
    Document doc;
    auto a = doc.add_page(100.0F, 100.0F, PageBackground::blank, 0.0F);
    auto b = doc.add_page(200.0F, 200.0F, PageBackground::grid, 25.0F);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, 0U);
    EXPECT_EQ(*b, 1U);
    EXPECT_EQ(doc.pages().size(), 2U);
}

TEST(DocumentPages, AddPagePreservesOriginX) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::lined, 77.5F);
    EXPECT_FLOAT_EQ(doc.pages().pages()[0].origin_x_px, 77.5F);
}

TEST(DocumentPages, RemovePageOutOfRangeRejects) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::blank, 0.0F);
    EXPECT_FALSE(doc.remove_page(5));
    EXPECT_EQ(doc.pages().size(), 1U);  // unchanged
}

TEST(DocumentPages, RemovePageReflowsLowerPages) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::blank, 0.0F);
    (void) *doc.add_page(200.0F, 200.0F, PageBackground::grid, 0.0F);
    ASSERT_TRUE(doc.remove_page(0));
    EXPECT_EQ(doc.pages().size(), 1U);
    EXPECT_EQ(doc.pages().pages()[0].background, PageBackground::grid);
    EXPECT_FLOAT_EQ(doc.pages().pages()[0].origin_y_px, 0.0F);
}

TEST(DocumentPages, InsertPageOutOfRangeRejects) {
    Document doc;
    Page p{};
    p.extent_w_px = 100.0F;
    p.extent_h_px = 100.0F;
    EXPECT_FALSE(doc.insert_page(5, p));
}

TEST(DocumentPages, InsertPageAcceptsEndIndex) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::blank, 0.0F);
    Page p{};
    p.extent_w_px = 100.0F;
    p.extent_h_px = 100.0F;
    p.background = PageBackground::dotted;
    auto idx = doc.insert_page(1, p);  // == size, appends
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
}

TEST(DocumentPages, ClearWipesPagesAndBlocks) {
    Document doc;
    (void) *doc.add_block(noted::domain::BlockKind::group, noted::domain::invalid_block_id, "r");
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::grid, 0.0F);
    doc.clear();
    EXPECT_TRUE(doc.empty());
    EXPECT_TRUE(doc.pages().empty());
}

TEST(DocumentPages, ReplacePagesInstallsFreshList) {
    Document doc;
    PageList fresh{40.0F};
    fresh.add_page(100.0F, 100.0F, PageBackground::grid);
    fresh.add_page(200.0F, 200.0F, PageBackground::lined);
    doc.replace_pages(std::move(fresh));
    EXPECT_EQ(doc.pages().size(), 2U);
    EXPECT_FLOAT_EQ(doc.pages().gap_px(), 40.0F);
    EXPECT_EQ(doc.pages().pages()[1].background, PageBackground::lined);
}
