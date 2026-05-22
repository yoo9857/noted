#include <gtest/gtest.h>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/document/document.hpp"

namespace {

using noted::domain::AddCanvasLayerCommand;
using noted::domain::AddStrokeCommand;
using noted::domain::Document;
using noted::domain::DuplicateCanvasLayerCommand;
using noted::domain::RemoveCanvasLayerCommand;
using noted::stroke::DrawMode;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;

[[nodiscard]] auto make_stroke() -> Stroke {
    Stroke s{};
    s.samples = {{.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
                 {.x = 5.0F, .y = 0.0F, .pressure = 1.0F}};
    s.mode = DrawMode::draw;
    return s;
}

TEST(DocumentCanvasLayers, FreshDocumentHasEmptyStackAndNoActive) {
    Document doc;
    EXPECT_TRUE(doc.canvas_layers().empty());
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
}

TEST(DocumentCanvasLayers, AddStrokeLazyCreatesDefaultLayer) {
    Document doc;
    auto idx = doc.add_stroke(make_stroke());
    ASSERT_TRUE(idx);
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    const auto active = doc.active_layer();
    EXPECT_NE(active, noted::invalid_layer_id);
    EXPECT_EQ(doc.strokes()[*idx].layer_id, active);
    EXPECT_EQ(doc.canvas_layers().layers()[0].name, "Layer 1");
}

TEST(DocumentCanvasLayers, AddStrokePreservesPreAssignedLayer) {
    Document doc;
    auto custom = doc.add_canvas_layer("Custom");
    ASSERT_TRUE(custom);
    Stroke s = make_stroke();
    s.layer_id = *custom;
    auto idx = doc.add_stroke(s);
    ASSERT_TRUE(idx);
    EXPECT_EQ(doc.strokes()[*idx].layer_id, *custom);
}

TEST(DocumentCanvasLayers, AddStrokeStampsActiveLayer) {
    Document doc;
    auto a = doc.add_canvas_layer("A");
    auto b = doc.add_canvas_layer("B");
    ASSERT_TRUE(a && b);
    // add_canvas_layer auto-promotes the first layer to active; the
    // second one does not change active. Set explicitly.
    ASSERT_TRUE(doc.set_active_layer(*b));
    auto idx = doc.add_stroke(make_stroke());
    ASSERT_TRUE(idx);
    EXPECT_EQ(doc.strokes()[*idx].layer_id, *b);
}

TEST(DocumentCanvasLayers, AddStrokeFallsBackToTopWhenActiveIsStale) {
    Document doc;
    auto a = doc.add_canvas_layer("A");
    auto b = doc.add_canvas_layer("B");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(doc.set_active_layer(*b));
    // Removing the active layer clears active to invalid; subsequent
    // add_stroke must still land on a real layer rather than silently
    // dropping to the unassigned sentinel.
    auto removed = doc.remove_canvas_layer(1);  // index 1 = b
    ASSERT_TRUE(removed);
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
    auto idx = doc.add_stroke(make_stroke());
    ASSERT_TRUE(idx);
    EXPECT_EQ(doc.strokes()[*idx].layer_id, *a);  // fell back to top (now only) layer
}

TEST(DocumentCanvasLayers, AddCanvasLayerPromotesFirstToActive) {
    Document doc;
    auto a = doc.add_canvas_layer("Layer 1");
    ASSERT_TRUE(a);
    EXPECT_EQ(doc.active_layer(), *a);
    auto b = doc.add_canvas_layer("Layer 2");
    ASSERT_TRUE(b);
    EXPECT_EQ(doc.active_layer(), *a);  // unchanged
}

TEST(DocumentCanvasLayers, SetActiveLayerRejectsUnknownId) {
    Document doc;
    EXPECT_FALSE(doc.set_active_layer(7777));
}

TEST(DocumentCanvasLayers, SetActiveLayerAcceptsInvalidIdToClear) {
    Document doc;
    auto a = doc.add_canvas_layer("L");
    ASSERT_TRUE(a);
    EXPECT_EQ(doc.active_layer(), *a);
    ASSERT_TRUE(doc.set_active_layer(noted::invalid_layer_id));
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
}

TEST(DocumentCanvasLayers, RemoveActiveLayerClearsActive) {
    Document doc;
    auto a = doc.add_canvas_layer("L");
    ASSERT_TRUE(a);
    EXPECT_EQ(doc.active_layer(), *a);
    auto removed = doc.remove_canvas_layer(0);
    ASSERT_TRUE(removed);
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
}

TEST(DocumentCanvasLayers, ReplaceCanvasLayersClearsActiveOnUnknown) {
    Document doc;
    auto a = doc.add_canvas_layer("L");
    ASSERT_TRUE(a);
    // Replace with an empty stack; the active id no longer resolves.
    doc.replace_canvas_layers(noted::domain::CanvasLayerStack{}, *a);
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
}

TEST(DocumentCanvasLayers, AddCanvasLayerCommandRoundTrip) {
    Document doc;
    AddCanvasLayerCommand cmd{"New Layer"};
    ASSERT_TRUE(cmd.apply(doc));
    const auto id = cmd.assigned_id();
    EXPECT_NE(id, noted::invalid_layer_id);
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.active_layer(), id);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_TRUE(doc.canvas_layers().empty());
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);

    // Redo
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.active_layer(), cmd.assigned_id());
}

TEST(DocumentCanvasLayers, AddCanvasLayerCommandRestoresPriorActive) {
    Document doc;
    auto first = doc.add_canvas_layer("First");
    ASSERT_TRUE(first);
    EXPECT_EQ(doc.active_layer(), *first);

    AddCanvasLayerCommand cmd{"Second"};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_NE(doc.active_layer(), *first);  // promoted to new layer

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.active_layer(), *first);  // prior active restored
}

TEST(DocumentCanvasLayers, RemoveCanvasLayerCommandRestoresLayerAndActive) {
    Document doc;
    (void) *doc.add_canvas_layer("Keep");
    auto b = *doc.add_canvas_layer("Drop");
    ASSERT_TRUE(doc.set_active_layer(b));
    EXPECT_EQ(doc.active_layer(), b);

    RemoveCanvasLayerCommand cmd{1};  // index 1 = b (top)
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, b);  // restored at original index
    EXPECT_EQ(doc.canvas_layers().layers()[1].name, "Drop");
    EXPECT_EQ(doc.active_layer(), b);  // active id restored
}

TEST(DocumentCanvasLayers, RemoveCanvasLayerCommandPreservesStrokeOrphans) {
    Document doc;
    auto a = *doc.add_canvas_layer("Source");
    Stroke s = make_stroke();
    s.layer_id = a;
    (void) *doc.add_stroke(s);

    RemoveCanvasLayerCommand cmd{0};
    ASSERT_TRUE(cmd.apply(doc));
    // Layer gone but stroke remains on disk with its original
    // (now-orphaned) layer_id — render filters it out.
    EXPECT_TRUE(doc.canvas_layers().empty());
    ASSERT_EQ(doc.strokes().size(), 1U);
    EXPECT_EQ(doc.strokes()[0].layer_id, a);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.strokes()[0].layer_id, a);  // still pinned
}

TEST(DocumentCanvasLayers, RemoveCanvasLayerCommandOutOfRangeRejects) {
    Document doc;
    RemoveCanvasLayerCommand cmd{5};
    EXPECT_FALSE(cmd.apply(doc));
}

TEST(DocumentCanvasLayers, AddStrokeCommandRedoPreservesLayer) {
    Document doc;
    auto a = doc.add_canvas_layer("A");
    auto b = doc.add_canvas_layer("B");
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(doc.set_active_layer(*a));

    // Build a command with an unstamped stroke; apply once.
    AddStrokeCommand cmd{make_stroke()};
    ASSERT_TRUE(cmd.apply(doc));
    const auto stamped = doc.strokes()[cmd.assigned_index()].layer_id;
    EXPECT_EQ(stamped, *a);

    // Undo, change active to b, redo. The redo must restore the
    // ORIGINALLY-stamped layer (a), not the now-active b — otherwise
    // a single undo/redo cycle silently re-targets the stroke.
    ASSERT_TRUE(cmd.undo(doc));
    ASSERT_TRUE(doc.set_active_layer(*b));
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.strokes()[cmd.assigned_index()].layer_id, *a);
}

// ---- DuplicateCanvasLayerCommand ------------------------------------------

TEST(DocumentCanvasLayers, DuplicateLayerClonesLayerAndStrokes) {
    Document doc;
    auto src = *doc.add_canvas_layer("Source");
    ASSERT_TRUE(doc.set_layer_opacity(src, 0.7F));
    ASSERT_TRUE(doc.set_layer_locked(src, true));

    // Two strokes on the source layer.
    Stroke s1 = make_stroke();
    s1.layer_id = src;
    (void) *doc.add_stroke(s1);
    Stroke s2 = make_stroke();
    s2.layer_id = src;
    (void) *doc.add_stroke(s2);

    DuplicateCanvasLayerCommand cmd{0};  // index 0 = src (only layer)
    ASSERT_TRUE(cmd.apply(doc));

    // New layer above source.
    ASSERT_EQ(doc.canvas_layers().size(), 2U);
    const auto dup_id = cmd.assigned_id();
    EXPECT_NE(dup_id, src);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, dup_id);  // dup is on top
    EXPECT_FLOAT_EQ(doc.canvas_layers().layers()[1].opacity, 0.7F);
    EXPECT_TRUE(doc.canvas_layers().layers()[1].locked);
    EXPECT_EQ(doc.active_layer(), dup_id);  // promoted to active

    // Strokes cloned: original two pinned to src + two clones pinned to dup.
    ASSERT_EQ(doc.strokes().size(), 4U);
    EXPECT_EQ(doc.strokes()[0].layer_id, src);
    EXPECT_EQ(doc.strokes()[1].layer_id, src);
    EXPECT_EQ(doc.strokes()[2].layer_id, dup_id);
    EXPECT_EQ(doc.strokes()[3].layer_id, dup_id);
}

TEST(DocumentCanvasLayers, DuplicateLayerUndoRemovesClone) {
    Document doc;
    auto src = *doc.add_canvas_layer("Source");
    Stroke s = make_stroke();
    s.layer_id = src;
    (void) *doc.add_stroke(s);

    DuplicateCanvasLayerCommand cmd{0};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.strokes().size(), 2U);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.strokes().size(), 1U);  // only the original survives
    EXPECT_EQ(doc.strokes()[0].layer_id, src);
    EXPECT_EQ(doc.active_layer(), src);  // prior active restored
}

TEST(DocumentCanvasLayers, DuplicateLayerSourceOutOfRangeRejects) {
    Document doc;
    DuplicateCanvasLayerCommand cmd{5};
    EXPECT_FALSE(cmd.apply(doc));
}

// ---- Lock + blend mode persistence ----------------------------------------

TEST(DocumentCanvasLayers, SetLayerLockedAndBlendRoundTrip) {
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    ASSERT_TRUE(doc.set_layer_locked(id, true));
    EXPECT_TRUE(doc.canvas_layers().find(id)->locked);
    ASSERT_TRUE(doc.set_layer_blend(id, noted::domain::BlendMode::multiply));
    EXPECT_EQ(doc.canvas_layers().find(id)->blend, noted::domain::BlendMode::multiply);
}

// ---- Document::clear regression -------------------------------------------

TEST(DocumentCanvasLayers, ClearWipesLayersAndActive) {
    Document doc;
    (void) *doc.add_canvas_layer("L");
    EXPECT_NE(doc.active_layer(), noted::invalid_layer_id);
    doc.clear();
    EXPECT_TRUE(doc.canvas_layers().empty());
    EXPECT_EQ(doc.active_layer(), noted::invalid_layer_id);
}

// ---- Move (reorder) regression --------------------------------------------

TEST(DocumentCanvasLayers, MoveCanvasLayerRotatesStack) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto b = *doc.add_canvas_layer("B");
    auto c = *doc.add_canvas_layer("C");
    // Order [A, B, C] (bottom-up). move(0, 2) → [B, C, A].
    ASSERT_TRUE(doc.move_canvas_layer(0, 2));
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, b);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, c);
    EXPECT_EQ(doc.canvas_layers().layers()[2].id, a);
}

}  // namespace
