#include <gtest/gtest.h>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/command/undo_stack.hpp"
#include "noted/domain/document/document.hpp"

namespace {

using noted::domain::AddCanvasLayerCommand;
using noted::domain::AddStrokeCommand;
using noted::domain::BlendMode;
using noted::domain::Document;
using noted::domain::DuplicateCanvasLayerCommand;
using noted::domain::FlattenImageCommand;
using noted::domain::MergeDownCommand;
using noted::domain::MergeVisibleCommand;
using noted::domain::MoveCanvasLayerCommand;
using noted::domain::RemoveCanvasLayerCommand;
using noted::domain::SetLayerBlendCommand;
using noted::domain::SetLayerLockedCommand;
using noted::domain::SetLayerNameCommand;
using noted::domain::SetLayerOpacityCommand;
using noted::domain::SetLayerVisibleCommand;
using noted::domain::UndoStack;
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

// ---- v1 of layer mutation commands (Move / SetVisible / SetLocked /
//      SetName / SetOpacity / SetBlend + UndoStack coalescing).
//
// Goal of these tests: lock in the user-facing contract that every
// row mutation in the Layers panel is undoable AND that the
// document's dirty proxy (undo-size delta) fires correctly. Before
// this layer landed, visibility / lock / opacity / blend / rename /
// reorder all bypassed the UndoStack — silently masking dirty state
// and dropping changes on close.

TEST(DocumentCanvasLayers, MoveCanvasLayerCommandRoundTripsUndoRedo) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto b = *doc.add_canvas_layer("B");
    auto c = *doc.add_canvas_layer("C");
    UndoStack stack;
    auto cmd = std::make_unique<MoveCanvasLayerCommand>(0U, 2U);
    ASSERT_TRUE(stack.execute(std::move(cmd), doc));
    EXPECT_EQ(doc.canvas_layers().layers()[2].id, a);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, a);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, b);
    EXPECT_EQ(doc.canvas_layers().layers()[2].id, c);
    ASSERT_TRUE(stack.redo(doc));
    EXPECT_EQ(doc.canvas_layers().layers()[2].id, a);
}

TEST(DocumentCanvasLayers, MoveCanvasLayerCommandRejectsOutOfRange) {
    Document doc;
    (void) *doc.add_canvas_layer("A");
    MoveCanvasLayerCommand cmd{0U, 5U};
    EXPECT_FALSE(cmd.apply(doc));
}

TEST(DocumentCanvasLayers, MoveCanvasLayerCommandNoOpSelfMoveIsCleanUndo) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<MoveCanvasLayerCommand>(0U, 0U), doc));
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, a);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, a);
}

TEST(DocumentCanvasLayers, SetLayerVisibleCommandUndoRestoresPrior) {
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerVisibleCommand>(id, false), doc));
    EXPECT_FALSE(doc.canvas_layers().find(id)->visible);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_TRUE(doc.canvas_layers().find(id)->visible);
}

TEST(DocumentCanvasLayers, SetLayerLockedCommandUndoRestoresPrior) {
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerLockedCommand>(id, true), doc));
    EXPECT_TRUE(doc.canvas_layers().find(id)->locked);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_FALSE(doc.canvas_layers().find(id)->locked);
}

TEST(DocumentCanvasLayers, SetLayerNameCommandUndoRestoresPrior) {
    Document doc;
    auto id = *doc.add_canvas_layer("Original");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(id, "Renamed"), doc));
    EXPECT_EQ(doc.canvas_layers().find(id)->name, "Renamed");
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(doc.canvas_layers().find(id)->name, "Original");
}

TEST(DocumentCanvasLayers, SetLayerNameCommandCoalescesSuccessiveSameTarget) {
    // Rename keystroke run on the same layer collapses to one undo
    // entry. The "before" name on the merged entry stays anchored at
    // the original — a single undo rewinds the entire rename gesture.
    Document doc;
    auto id = *doc.add_canvas_layer("Original");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(id, "Re"), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(id, "Rena"), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(id, "Renamed"), doc));
    EXPECT_EQ(stack.undo_size(), 1U);
    EXPECT_EQ(doc.canvas_layers().find(id)->name, "Renamed");
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(doc.canvas_layers().find(id)->name, "Original");
    EXPECT_EQ(stack.undo_size(), 0U);
}

TEST(DocumentCanvasLayers, SetLayerNameCommandDoesNotCoalesceAcrossLayers) {
    Document doc;
    auto a = *doc.add_canvas_layer("A0");
    auto b = *doc.add_canvas_layer("B0");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(a, "A1"), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(b, "B1"), doc));
    EXPECT_EQ(stack.undo_size(), 2U);
}

TEST(DocumentCanvasLayers, SetLayerOpacityCommandUndoRestoresPrior) {
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, 0.25F), doc));
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(id)->opacity, 0.25F);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(id)->opacity, 1.0F);
}

TEST(DocumentCanvasLayers, SetLayerOpacityCommandCoalescesSliderDrag) {
    // 60-Hz drag simulation: push 30 distinct opacity values for the
    // same layer. UndoStack must fold them into ONE entry whose
    // "after" is the last value and "before" is the pre-drag value.
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    for (int i = 0; i < 30; ++i) {
        const float v = 1.0F - (static_cast<float>(i) * 0.01F);
        ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, v), doc));
    }
    EXPECT_EQ(stack.undo_size(), 1U);
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(id)->opacity, 1.0F - 29.0F * 0.01F);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(id)->opacity, 1.0F);
}

TEST(DocumentCanvasLayers, SetLayerOpacityCommandDoesNotCoalesceAcrossLayers) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto b = *doc.add_canvas_layer("B");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(a, 0.5F), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(b, 0.5F), doc));
    EXPECT_EQ(stack.undo_size(), 2U);
}

TEST(DocumentCanvasLayers, SetLayerOpacityCommandCoalesceDoesNotBridgeOtherCommand) {
    // Drag opacity → unrelated rename → drag opacity again. The
    // second drag must NOT absorb into the first (the intervening
    // rename broke the chain at the top of stack).
    Document doc;
    auto id = *doc.add_canvas_layer("Name1");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, 0.7F), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerNameCommand>(id, "Name2"), doc));
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, 0.3F), doc));
    EXPECT_EQ(stack.undo_size(), 3U);
}

TEST(DocumentCanvasLayers, SetLayerBlendCommandUndoRestoresPrior) {
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    ASSERT_TRUE(
        stack.execute(std::make_unique<SetLayerBlendCommand>(id, BlendMode::multiply), doc));
    EXPECT_EQ(doc.canvas_layers().find(id)->blend, BlendMode::multiply);
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(doc.canvas_layers().find(id)->blend, BlendMode::normal);
}

TEST(DocumentCanvasLayers, SetLayerFieldCommandsAllRejectUnknownId) {
    Document doc;
    constexpr noted::LayerId kBogus = 99999;
    SetLayerVisibleCommand vis{kBogus, false};
    SetLayerLockedCommand lock{kBogus, true};
    SetLayerNameCommand name{kBogus, "x"};
    SetLayerOpacityCommand op{kBogus, 0.5F};
    SetLayerBlendCommand blend{kBogus, BlendMode::multiply};
    EXPECT_FALSE(vis.apply(doc));
    EXPECT_FALSE(lock.apply(doc));
    EXPECT_FALSE(name.apply(doc));
    EXPECT_FALSE(op.apply(doc));
    EXPECT_FALSE(blend.apply(doc));
}

// ---- Merge Down -----------------------------------------------------------

TEST(DocumentCanvasLayers, MergeDownMigratesStrokesAndRemovesSource) {
    Document doc;
    auto bottom = *doc.add_canvas_layer("Bottom");
    auto top = *doc.add_canvas_layer("Top");
    ASSERT_TRUE(doc.set_layer_opacity(top, 0.5F));  // half-baked into strokes
    // Strokes on both layers.
    Stroke s_bottom = make_stroke();
    s_bottom.layer_id = bottom;
    s_bottom.style.a = 1.0F;
    ASSERT_TRUE(doc.add_stroke(s_bottom));
    Stroke s_top = make_stroke();
    s_top.layer_id = top;
    s_top.style.a = 0.8F;
    ASSERT_TRUE(doc.add_stroke(s_top));

    UndoStack stack;
    // Top is at index 1; merge it down into Bottom (index 0).
    ASSERT_TRUE(stack.execute(std::make_unique<MergeDownCommand>(1U), doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, bottom);
    EXPECT_EQ(doc.active_layer(), bottom);

    // Both strokes now pinned to bottom; the top stroke's alpha is
    // multiplied by 0.5 (the source opacity).
    ASSERT_EQ(doc.strokes().size(), 2U);
    EXPECT_EQ(doc.strokes()[0].layer_id, bottom);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 1.0F);
    EXPECT_EQ(doc.strokes()[1].layer_id, bottom);
    EXPECT_FLOAT_EQ(doc.strokes()[1].style.a, 0.8F * 0.5F);
}

TEST(DocumentCanvasLayers, MergeDownUndoRestoresEverything) {
    Document doc;
    auto bottom = *doc.add_canvas_layer("Bottom");
    auto top = *doc.add_canvas_layer("Top");
    ASSERT_TRUE(doc.set_layer_opacity(top, 0.5F));
    Stroke s_top = make_stroke();
    s_top.layer_id = top;
    s_top.style.a = 0.8F;
    ASSERT_TRUE(doc.add_stroke(s_top));
    ASSERT_TRUE(doc.set_active_layer(top));

    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<MergeDownCommand>(1U), doc));
    ASSERT_TRUE(stack.undo(doc));

    // Layer back at its original index, with name + opacity preserved.
    ASSERT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, bottom);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, top);
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(top)->opacity, 0.5F);
    // Stroke back on `top` with original alpha — not the baked one.
    ASSERT_EQ(doc.strokes().size(), 1U);
    EXPECT_EQ(doc.strokes()[0].layer_id, top);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 0.8F);
    // Active id restored.
    EXPECT_EQ(doc.active_layer(), top);
}

TEST(DocumentCanvasLayers, MergeDownRejectsBottomMost) {
    Document doc;
    (void) *doc.add_canvas_layer("Only");
    MergeDownCommand cmd{0U};
    EXPECT_FALSE(cmd.apply(doc));
}

TEST(DocumentCanvasLayers, MergeDownClampsAlphaToUnit) {
    // 1.0 alpha × 1.0 opacity must NOT exceed 1.0 due to float
    // arithmetic. Sanity check the clamp at the data boundary.
    Document doc;
    (void) *doc.add_canvas_layer("Bottom");
    auto top = *doc.add_canvas_layer("Top");
    ASSERT_TRUE(doc.set_layer_opacity(top, 1.0F));
    Stroke s = make_stroke();
    s.layer_id = top;
    s.style.a = 1.0F;
    ASSERT_TRUE(doc.add_stroke(s));

    MergeDownCommand cmd{1U};
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_LE(doc.strokes()[0].style.a, 1.0F);
    EXPECT_GE(doc.strokes()[0].style.a, 0.99F);  // ~1.0, just clamped
}

TEST(DocumentCanvasLayers, MergeDownLeavesOtherLayerStrokesUntouched) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");  // index 0 (bottom)
    auto b = *doc.add_canvas_layer("B");  // index 1 (target of merge)
    auto c = *doc.add_canvas_layer("C");  // index 2 (source of merge)
    Stroke s_a = make_stroke();
    s_a.layer_id = a;
    s_a.style.a = 0.3F;
    ASSERT_TRUE(doc.add_stroke(s_a));
    Stroke s_b = make_stroke();
    s_b.layer_id = b;
    s_b.style.a = 0.6F;
    ASSERT_TRUE(doc.add_stroke(s_b));
    Stroke s_c = make_stroke();
    s_c.layer_id = c;
    s_c.style.a = 0.9F;
    ASSERT_TRUE(doc.add_stroke(s_c));
    ASSERT_TRUE(doc.set_layer_opacity(c, 0.5F));

    MergeDownCommand cmd{2U};  // merge C into B
    ASSERT_TRUE(cmd.apply(doc));

    // Layer A and its stroke are untouched.
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 0.3F);
    EXPECT_EQ(doc.strokes()[0].layer_id, a);
    // B's stroke is untouched.
    EXPECT_FLOAT_EQ(doc.strokes()[1].style.a, 0.6F);
    EXPECT_EQ(doc.strokes()[1].layer_id, b);
    // C's stroke moves to B with baked alpha.
    EXPECT_EQ(doc.strokes()[2].layer_id, b);
    EXPECT_FLOAT_EQ(doc.strokes()[2].style.a, 0.9F * 0.5F);
}

// ---- Merge Visible --------------------------------------------------------

TEST(DocumentCanvasLayers, MergeVisibleCollapsesVisibleIntoBottomMost) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");  // bottom-most, visible
    auto b = *doc.add_canvas_layer("B");  // middle, hidden
    auto c = *doc.add_canvas_layer("C");  // top, visible
    ASSERT_TRUE(doc.set_layer_visible(b, false));
    ASSERT_TRUE(doc.set_layer_opacity(c, 0.5F));
    // Strokes on each layer.
    Stroke s_a = make_stroke();
    s_a.layer_id = a;
    s_a.style.a = 1.0F;
    ASSERT_TRUE(doc.add_stroke(s_a));
    Stroke s_b = make_stroke();
    s_b.layer_id = b;
    s_b.style.a = 0.7F;
    ASSERT_TRUE(doc.add_stroke(s_b));
    Stroke s_c = make_stroke();
    s_c.layer_id = c;
    s_c.style.a = 0.8F;
    ASSERT_TRUE(doc.add_stroke(s_c));

    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<MergeVisibleCommand>(), doc));
    // After: only B and A remain (B was hidden, untouched). C
    // merged into A; sink = A (bottom-most visible).
    ASSERT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, a);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, b);
    EXPECT_EQ(doc.active_layer(), a);
    // All 3 strokes preserved. A's stroke unchanged. B's stroke
    // untouched (hidden). C's stroke moves to A with baked alpha.
    ASSERT_EQ(doc.strokes().size(), 3U);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 1.0F);
    EXPECT_EQ(doc.strokes()[0].layer_id, a);
    EXPECT_FLOAT_EQ(doc.strokes()[1].style.a, 0.7F);
    EXPECT_EQ(doc.strokes()[1].layer_id, b);  // hidden — untouched
    EXPECT_EQ(doc.strokes()[2].layer_id, a);
    EXPECT_FLOAT_EQ(doc.strokes()[2].style.a, 0.8F * 0.5F);
}

TEST(DocumentCanvasLayers, MergeVisibleUndoRestoresStackAndStrokes) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto c = *doc.add_canvas_layer("C");
    ASSERT_TRUE(doc.set_layer_opacity(c, 0.5F));
    Stroke s_c = make_stroke();
    s_c.layer_id = c;
    s_c.style.a = 0.8F;
    ASSERT_TRUE(doc.add_stroke(s_c));

    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<MergeVisibleCommand>(), doc));
    ASSERT_TRUE(stack.undo(doc));
    ASSERT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, c);
    EXPECT_FLOAT_EQ(doc.canvas_layers().find(c)->opacity, 0.5F);
    EXPECT_EQ(doc.strokes()[0].layer_id, c);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 0.8F);
    (void) a;
}

TEST(DocumentCanvasLayers, MergeVisibleIsNoOpForSingleVisibleLayer) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto b = *doc.add_canvas_layer("B");
    ASSERT_TRUE(doc.set_layer_visible(b, false));
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<MergeVisibleCommand>(), doc));
    EXPECT_EQ(doc.canvas_layers().size(), 2U);  // unchanged
    (void) a;
}

// ---- Flatten Image --------------------------------------------------------

TEST(DocumentCanvasLayers, FlattenImageMergesVisibleAndDropsHidden) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");  // visible, kept as sink
    auto b = *doc.add_canvas_layer("B");  // hidden — dropped
    auto c = *doc.add_canvas_layer("C");  // visible — merged into A
    ASSERT_TRUE(doc.set_layer_visible(b, false));
    Stroke s_a = make_stroke();
    s_a.layer_id = a;
    s_a.style.a = 1.0F;
    ASSERT_TRUE(doc.add_stroke(s_a));
    Stroke s_b = make_stroke();
    s_b.layer_id = b;  // hidden stroke — should be dropped
    s_b.style.a = 0.5F;
    ASSERT_TRUE(doc.add_stroke(s_b));
    Stroke s_c = make_stroke();
    s_c.layer_id = c;
    s_c.style.a = 0.8F;
    ASSERT_TRUE(doc.add_stroke(s_c));

    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<FlattenImageCommand>(), doc));
    // Only the sink (A) remains.
    ASSERT_EQ(doc.canvas_layers().size(), 1U);
    EXPECT_EQ(doc.canvas_layers().layers()[0].id, a);
    EXPECT_EQ(doc.active_layer(), a);
    // 2 strokes: A's original + C's merged. B's hidden stroke is
    // gone.
    ASSERT_EQ(doc.strokes().size(), 2U);
    EXPECT_EQ(doc.strokes()[0].layer_id, a);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 1.0F);
    EXPECT_EQ(doc.strokes()[1].layer_id, a);
    EXPECT_FLOAT_EQ(doc.strokes()[1].style.a, 0.8F);
}

TEST(DocumentCanvasLayers, FlattenImageUndoRestoresHiddenLayerAndStrokes) {
    Document doc;
    auto a = *doc.add_canvas_layer("A");
    auto b = *doc.add_canvas_layer("B");
    ASSERT_TRUE(doc.set_layer_visible(b, false));
    Stroke s_b = make_stroke();
    s_b.layer_id = b;
    s_b.style.a = 0.5F;
    ASSERT_TRUE(doc.add_stroke(s_b));

    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<FlattenImageCommand>(), doc));
    ASSERT_TRUE(stack.undo(doc));
    ASSERT_EQ(doc.canvas_layers().size(), 2U);
    EXPECT_EQ(doc.canvas_layers().layers()[1].id, b);
    ASSERT_EQ(doc.strokes().size(), 1U);
    EXPECT_EQ(doc.strokes()[0].layer_id, b);
    EXPECT_FLOAT_EQ(doc.strokes()[0].style.a, 0.5F);
    (void) a;
}

TEST(DocumentCanvasLayers, FlattenImageIsNoOpForAlreadyFlat) {
    Document doc;
    (void) *doc.add_canvas_layer("Only");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<FlattenImageCommand>(), doc));
    EXPECT_EQ(doc.canvas_layers().size(), 1U);
}

TEST(DocumentCanvasLayers, FieldCommandClearsRedoStackEvenOnCoalesce) {
    // A new edit invalidates any outstanding redo path, regardless of
    // whether it ended up coalesced into the prior entry. Otherwise a
    // slider tweak right after an undo could "redo" a stale state.
    Document doc;
    auto id = *doc.add_canvas_layer("L");
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, 0.5F), doc));
    ASSERT_TRUE(stack.undo(doc));
    EXPECT_EQ(stack.redo_size(), 1U);
    ASSERT_TRUE(stack.execute(std::make_unique<SetLayerOpacityCommand>(id, 0.6F), doc));
    EXPECT_EQ(stack.redo_size(), 0U);
}

}  // namespace
