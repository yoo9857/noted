#include "noted/domain/clipboard/clipboard.hpp"

#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "noted/domain/clipboard/selection_ops.hpp"
#include "noted/domain/command/commands.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/shape_drag.hpp"

namespace {

using noted::domain::Clipboard;
using noted::domain::DeleteShapesCommand;
using noted::domain::Document;
using noted::domain::PasteShapesCommand;
using noted::domain::Selection;
using noted::domain::SelectionRect;
using noted::domain::shapes_in_selection;
using noted::domain::tool::ShapeKind;
using noted::domain::tool::ShapePrimitive;

[[nodiscard]] auto make_rect_shape(double x0, double y0, double x1, double y1) -> ShapePrimitive {
    ShapePrimitive s{};
    s.kind = ShapeKind::rectangle;
    s.x0 = x0;
    s.y0 = y0;
    s.x1 = x1;
    s.y1 = y1;
    return s;
}

}  // namespace

// ---- Clipboard --------------------------------------------------------------

TEST(Clipboard, DefaultIsEmpty) {
    Clipboard cb;
    EXPECT_TRUE(cb.is_empty());
    EXPECT_FALSE(cb.has_shapes());
    EXPECT_TRUE(cb.shapes().empty());
}

TEST(Clipboard, SetReplacesPreviousContent) {
    Clipboard cb;
    cb.set_shapes({make_rect_shape(0, 0, 10, 10)});
    EXPECT_EQ(cb.shapes().size(), 1U);
    cb.set_shapes({make_rect_shape(0, 0, 5, 5), make_rect_shape(20, 20, 30, 30)});
    EXPECT_EQ(cb.shapes().size(), 2U);
}

TEST(Clipboard, ClearEmpties) {
    Clipboard cb;
    cb.set_shapes({make_rect_shape(0, 0, 10, 10)});
    cb.clear();
    EXPECT_TRUE(cb.is_empty());
}

// ---- shapes_in_selection ----------------------------------------------------

TEST(ShapesInSelection, EmptySelectionYieldsEmpty) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    Selection sel;
    EXPECT_TRUE(shapes_in_selection(doc, sel).empty());
}

TEST(ShapesInSelection, SelectsByCentre) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));    // centre (5,5)
    (void) doc.add_shape(make_rect_shape(20, 20, 30, 30));  // centre (25,25)
    (void) doc.add_shape(make_rect_shape(50, 50, 60, 60));  // centre (55,55)

    auto sel = Selection::from_rect(SelectionRect{0, 0, 40, 40});  // covers shape 0 + 1
    const auto picks = shapes_in_selection(doc, sel);
    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0], 0U);
    EXPECT_EQ(picks[1], 1U);
}

TEST(ShapesInSelection, IndicesAreAscending) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    (void) doc.add_shape(make_rect_shape(50, 50, 60, 60));
    (void) doc.add_shape(make_rect_shape(20, 20, 30, 30));
    auto sel = Selection::from_rect(SelectionRect{0, 0, 100, 100});
    const auto picks = shapes_in_selection(doc, sel);
    ASSERT_EQ(picks.size(), 3U);
    EXPECT_EQ(picks[0], 0U);
    EXPECT_EQ(picks[1], 1U);
    EXPECT_EQ(picks[2], 2U);
}

TEST(ShapesInSelection, MultiRectUnionWorks) {
    // Two disjoint selection rects — a shape in the left rect AND a
    // shape in the right rect should both be picked.
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));    // centre (5,5) - in left
    (void) doc.add_shape(make_rect_shape(80, 0, 90, 10));   // centre (85,5) - in right
    (void) doc.add_shape(make_rect_shape(40, 40, 50, 50));  // centre (45,45) - in NEITHER

    Selection sel;
    sel.add_rect(SelectionRect{0, 0, 20, 20});
    sel.add_rect(SelectionRect{70, 0, 30, 20});

    const auto picks = shapes_in_selection(doc, sel);
    ASSERT_EQ(picks.size(), 2U);
    EXPECT_EQ(picks[0], 0U);
    EXPECT_EQ(picks[1], 1U);
}

// ---- DeleteShapesCommand ----------------------------------------------------

TEST(DeleteShapesCommand, DeletesAndUndoRestores) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));    // 0
    (void) doc.add_shape(make_rect_shape(20, 20, 30, 30));  // 1
    (void) doc.add_shape(make_rect_shape(40, 40, 50, 50));  // 2

    DeleteShapesCommand cmd{{0U, 2U}};  // remove 0 and 2, keep 1
    ASSERT_TRUE(cmd.apply(doc));
    ASSERT_EQ(doc.shapes().size(), 1U);
    // The surviving shape is the one at original index 1 → bounds (20,20,30,30).
    EXPECT_EQ(doc.shapes()[0].x0, 20);

    ASSERT_TRUE(cmd.undo(doc));
    ASSERT_EQ(doc.shapes().size(), 3U);
    EXPECT_EQ(doc.shapes()[0].x0, 0);
    EXPECT_EQ(doc.shapes()[1].x0, 20);
    EXPECT_EQ(doc.shapes()[2].x0, 40);
}

TEST(DeleteShapesCommand, EmptyIndicesIsNoOp) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    DeleteShapesCommand cmd{{}};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.shapes().size(), 1U);
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.shapes().size(), 1U);
}

TEST(DeleteShapesCommand, RejectsOutOfRangeAndLeavesDocUntouched) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    DeleteShapesCommand cmd{{0U, 5U}};  // index 5 is out of range
    auto r = cmd.apply(doc);
    EXPECT_FALSE(r);
    EXPECT_EQ(doc.shapes().size(), 1U);  // doc untouched
}

TEST(DeleteShapesCommand, DuplicateIndicesAreDedupedFirst) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    (void) doc.add_shape(make_rect_shape(20, 20, 30, 30));
    DeleteShapesCommand cmd{{0U, 0U, 1U, 1U}};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_TRUE(doc.shapes().empty());
}

// ---- PasteShapesCommand -----------------------------------------------------

TEST(PasteShapesCommand, AppendsAndUndoRemoves) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));  // existing
    PasteShapesCommand cmd{
        {make_rect_shape(100, 100, 110, 110), make_rect_shape(120, 120, 130, 130)}};
    ASSERT_TRUE(cmd.apply(doc));
    ASSERT_EQ(doc.shapes().size(), 3U);
    EXPECT_EQ(cmd.first_assigned_index(), 1U);
    EXPECT_EQ(cmd.count(), 2U);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.shapes().size(), 1U);
    EXPECT_EQ(doc.shapes()[0].x0, 0);  // original survivor
}

TEST(PasteShapesCommand, EmptyPasteIsNoOp) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));
    PasteShapesCommand cmd{{}};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.shapes().size(), 1U);
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.shapes().size(), 1U);
}

// ---- Round-trip Cut (Copy + Delete) + Paste --------------------------------

TEST(ClipboardEnd2End, CutCopyPasteRoundTrip) {
    Document doc;
    (void) doc.add_shape(make_rect_shape(0, 0, 10, 10));    // 0
    (void) doc.add_shape(make_rect_shape(20, 20, 30, 30));  // 1
    (void) doc.add_shape(make_rect_shape(40, 40, 50, 50));  // 2

    // User selects the area covering shape 1 only.
    auto sel = Selection::from_rect(SelectionRect{15, 15, 20, 20});
    const auto picks = shapes_in_selection(doc, sel);
    ASSERT_EQ(picks.size(), 1U);

    // Copy: snapshot into the clipboard.
    Clipboard cb;
    std::vector<ShapePrimitive> copied;
    for (auto idx : picks) {
        copied.push_back(doc.shapes()[idx]);
    }
    cb.set_shapes(std::move(copied));
    EXPECT_TRUE(cb.has_shapes());

    // Cut: delete the same indices that were copied.
    DeleteShapesCommand del{picks};
    ASSERT_TRUE(del.apply(doc));
    EXPECT_EQ(doc.shapes().size(), 2U);

    // Paste: append the clipboard contents.
    PasteShapesCommand paste{cb.shapes()};
    ASSERT_TRUE(paste.apply(doc));
    EXPECT_EQ(doc.shapes().size(), 3U);
    // The pasted shape is appended; its bounds match the cut shape's.
    EXPECT_EQ(doc.shapes().back().x0, 20);
    EXPECT_EQ(doc.shapes().back().x1, 30);
}
