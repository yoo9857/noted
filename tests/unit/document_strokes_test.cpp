#include <gtest/gtest.h>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/document/document.hpp"

namespace {

using noted::domain::AddStrokeCommand;
using noted::domain::Document;
using noted::domain::RemoveStrokeCommand;
using noted::stroke::BrushStyle;
using noted::stroke::DrawMode;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;

[[nodiscard]] auto make_stroke(float x, float y) -> Stroke {
    Stroke s;
    s.samples = {{.x = x, .y = y, .pressure = 1.0F}, {.x = x + 10.0F, .y = y, .pressure = 0.5F}};
    s.style.r = 0.2F;
    s.style.g = 0.4F;
    s.style.b = 0.6F;
    s.style.a = 1.0F;
    s.style.min_radius_px = 2.0F;
    s.style.max_radius_px = 8.0F;
    s.style.stabilizer = 0.3F;
    s.mode = DrawMode::draw;
    return s;
}

}  // namespace

TEST(DocumentStrokes, FreshDocumentHasEmptyStrokes) {
    Document doc;
    EXPECT_TRUE(doc.strokes().empty());
}

TEST(DocumentStrokes, AddStrokeAssignsAppendIndex) {
    Document doc;
    auto a = doc.add_stroke(make_stroke(0.0F, 0.0F));
    auto b = doc.add_stroke(make_stroke(100.0F, 100.0F));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(*a, 0U);
    EXPECT_EQ(*b, 1U);
    EXPECT_EQ(doc.strokes().size(), 2U);
}

TEST(DocumentStrokes, AddStrokePreservesAllFields) {
    Document doc;
    auto s = make_stroke(7.5F, 8.5F);
    s.style.stabilizer = 0.7F;
    s.mode = DrawMode::erase;
    (void) *doc.add_stroke(s);
    const auto& got = doc.strokes()[0];
    EXPECT_FLOAT_EQ(got.samples[0].x, 7.5F);
    EXPECT_FLOAT_EQ(got.samples[1].pressure, 0.5F);
    EXPECT_FLOAT_EQ(got.style.stabilizer, 0.7F);
    EXPECT_EQ(got.mode, DrawMode::erase);
}

TEST(DocumentStrokes, RemoveStrokeOutOfRangeRejects) {
    Document doc;
    EXPECT_FALSE(doc.remove_stroke(0));
}

TEST(DocumentStrokes, RemoveStrokeReflowsLowerStrokes) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    (void) *doc.add_stroke(make_stroke(100.0F, 100.0F));
    (void) *doc.add_stroke(make_stroke(200.0F, 200.0F));
    ASSERT_TRUE(doc.remove_stroke(1));
    ASSERT_EQ(doc.strokes().size(), 2U);
    EXPECT_FLOAT_EQ(doc.strokes()[0].samples[0].x, 0.0F);
    EXPECT_FLOAT_EQ(doc.strokes()[1].samples[0].x, 200.0F);
}

TEST(DocumentStrokes, InsertStrokeAtMiddleShifts) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    (void) *doc.add_stroke(make_stroke(200.0F, 200.0F));
    auto idx = doc.insert_stroke(1, make_stroke(100.0F, 100.0F));
    ASSERT_TRUE(idx);
    EXPECT_EQ(*idx, 1U);
    EXPECT_FLOAT_EQ(doc.strokes()[1].samples[0].x, 100.0F);
}

TEST(DocumentStrokes, ReplaceStrokesSwapsList) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    std::vector<Stroke> repl;
    repl.push_back(make_stroke(500.0F, 500.0F));
    doc.replace_strokes(std::move(repl));
    ASSERT_EQ(doc.strokes().size(), 1U);
    EXPECT_FLOAT_EQ(doc.strokes()[0].samples[0].x, 500.0F);
}

TEST(DocumentStrokes, ClearEmptiesStrokes) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    doc.clear();
    EXPECT_TRUE(doc.strokes().empty());
}

// ---- AddStrokeCommand ------------------------------------------------------

TEST(StrokeCommands, AddStrokeCommandAppliesAndUndoes) {
    Document doc;
    auto cmd = std::make_unique<AddStrokeCommand>(make_stroke(0.0F, 0.0F));
    auto* raw = cmd.get();
    ASSERT_TRUE(raw->apply(doc));
    EXPECT_EQ(doc.strokes().size(), 1U);
    EXPECT_EQ(raw->assigned_index(), 0U);

    ASSERT_TRUE(raw->undo(doc));
    EXPECT_TRUE(doc.strokes().empty());
}

TEST(StrokeCommands, AddStrokeUndoRejectsWhenNotApplied) {
    Document doc;
    AddStrokeCommand cmd{make_stroke(0.0F, 0.0F)};
    EXPECT_FALSE(cmd.undo(doc));
}

// ---- RemoveStrokeCommand --------------------------------------------------

TEST(StrokeCommands, RemoveStrokeRoundTripPreservesSamples) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    (void) *doc.add_stroke(make_stroke(50.0F, 50.0F));

    RemoveStrokeCommand cmd{0};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.strokes().size(), 1U);
    EXPECT_FLOAT_EQ(doc.strokes()[0].samples[0].x, 50.0F);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.strokes().size(), 2U);
    EXPECT_FLOAT_EQ(doc.strokes()[0].samples[0].x, 0.0F);
    EXPECT_FLOAT_EQ(doc.strokes()[1].samples[0].x, 50.0F);
}

TEST(StrokeCommands, RemoveStrokeOutOfRangeRejects) {
    Document doc;
    (void) *doc.add_stroke(make_stroke(0.0F, 0.0F));
    RemoveStrokeCommand cmd{5};
    EXPECT_FALSE(cmd.apply(doc));
}
