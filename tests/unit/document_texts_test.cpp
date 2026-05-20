#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/tool/text_input.hpp"

namespace {

using noted::domain::Document;
using noted::domain::tool::TextPrimitive;

[[nodiscard]] auto make_text(double x, double y, std::string s) noexcept -> TextPrimitive {
    TextPrimitive p{};
    p.x = x;
    p.y = y;
    p.content = std::move(s);
    p.font_size_px = 16.0F;
    p.a = 1.0F;
    return p;
}

}  // namespace

TEST(DocumentTexts, FreshDocumentHasEmptyTexts) {
    Document doc;
    EXPECT_TRUE(doc.texts().empty());
}

TEST(DocumentTexts, AddTextAssignsAppendIndex) {
    Document doc;
    auto a = doc.add_text(make_text(0.0, 0.0, "a"));
    auto b = doc.add_text(make_text(10.0, 10.0, "b"));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, 0U);
    EXPECT_EQ(*b, 1U);
    EXPECT_EQ(doc.texts().size(), 2U);
}

TEST(DocumentTexts, AddTextPreservesContent) {
    Document doc;
    (void) *doc.add_text(make_text(0.0, 0.0, "hello"));
    EXPECT_EQ(doc.texts()[0].content, "hello");
}

TEST(DocumentTexts, RemoveTextOutOfRangeRejects) {
    Document doc;
    EXPECT_FALSE(doc.remove_text(0));
}

TEST(DocumentTexts, RemoveTextReflowsLowerTexts) {
    Document doc;
    (void) *doc.add_text(make_text(0.0, 0.0, "a"));
    (void) *doc.add_text(make_text(1.0, 1.0, "b"));
    (void) *doc.add_text(make_text(2.0, 2.0, "c"));
    ASSERT_TRUE(doc.remove_text(1));
    ASSERT_EQ(doc.texts().size(), 2U);
    EXPECT_EQ(doc.texts()[0].content, "a");
    EXPECT_EQ(doc.texts()[1].content, "c");
}

TEST(DocumentTexts, InsertTextAtMiddleShifts) {
    Document doc;
    (void) *doc.add_text(make_text(0.0, 0.0, "a"));
    (void) *doc.add_text(make_text(2.0, 2.0, "c"));
    auto idx = doc.insert_text(1, make_text(1.0, 1.0, "b"));
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
    EXPECT_EQ(doc.texts()[0].content, "a");
    EXPECT_EQ(doc.texts()[1].content, "b");
    EXPECT_EQ(doc.texts()[2].content, "c");
}

TEST(DocumentTexts, ReplaceTextsSwapsList) {
    Document doc;
    (void) *doc.add_text(make_text(0.0, 0.0, "a"));
    std::vector<TextPrimitive> repl;
    repl.push_back(make_text(10.0, 10.0, "new"));
    doc.replace_texts(repl);
    ASSERT_EQ(doc.texts().size(), 1U);
    EXPECT_EQ(doc.texts()[0].content, "new");
}

TEST(DocumentTexts, ClearEmptiesTexts) {
    Document doc;
    (void) *doc.add_text(make_text(0.0, 0.0, "a"));
    doc.clear();
    EXPECT_TRUE(doc.texts().empty());
}
