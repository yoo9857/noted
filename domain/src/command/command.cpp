#include <algorithm>
#include <utility>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/command/undo_stack.hpp"

namespace noted::domain {

namespace {

// Pre-order walk of the subtree rooted at `start`, copying each
// BlockNode into `out`. Stable order — guarantees parents precede
// their children, which is the invariant `restore_subtree` requires.
void snapshot_subtree(const Document& doc, BlockId start, std::vector<BlockNode>& out) {
    std::vector<BlockId> stack;
    stack.push_back(start);
    while (!stack.empty()) {
        const auto id = stack.back();
        stack.pop_back();
        const auto* node = doc.find(id);
        if (node == nullptr) {
            continue;
        }
        out.push_back(*node);
        // Push children in reverse so the leftmost child is popped first.
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

// Locate the index of `id` in its parent's children list. Returns
// (parent_id, index). For the document root returns
// (invalid_block_id, 0).
auto find_position(const Document& doc, BlockId id) -> std::pair<BlockId, std::size_t> {
    const auto* node = doc.find(id);
    if (node == nullptr || node->parent == invalid_block_id) {
        return {invalid_block_id, 0};
    }
    const auto* parent = doc.find(node->parent);
    if (parent == nullptr) {
        return {invalid_block_id, 0};
    }
    const auto it = std::find(parent->children.begin(), parent->children.end(), id);
    if (it == parent->children.end()) {
        return {node->parent, 0};
    }
    return {node->parent, static_cast<std::size_t>(it - parent->children.begin())};
}

}  // namespace

// ============================================================================
// AddBlockCommand
// ============================================================================

AddBlockCommand::AddBlockCommand(BlockKind kind, BlockId parent, std::string name)
    : kind_(kind), parent_(parent), name_(std::move(name)) {}

auto AddBlockCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_block(kind_, parent_, name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = *r;
    was_root_alloc_ = (parent_ == invalid_block_id);
    return {};
}

auto AddBlockCommand::undo(Document& doc) -> Result<void> {
    if (assigned_id_ == invalid_block_id) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddBlockCommand::undo: command was not applied"));
    }
    auto r = doc.remove_block(assigned_id_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = invalid_block_id;
    was_root_alloc_ = false;
    return {};
}

// ============================================================================
// InsertBlockCommand
// ============================================================================

InsertBlockCommand::InsertBlockCommand(BlockKind kind,
                                       BlockId parent,
                                       std::size_t index,
                                       std::string name)
    : kind_(kind), parent_(parent), index_(index), name_(std::move(name)) {}

auto InsertBlockCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.insert_block(kind_, parent_, index_, name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = *r;
    return {};
}

auto InsertBlockCommand::undo(Document& doc) -> Result<void> {
    if (assigned_id_ == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "InsertBlockCommand::undo: command was not applied"));
    }
    auto r = doc.remove_block(assigned_id_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = invalid_block_id;
    return {};
}

// ============================================================================
// RemoveBlockCommand
// ============================================================================

RemoveBlockCommand::RemoveBlockCommand(BlockId target) : target_(target) {}

auto RemoveBlockCommand::apply(Document& doc) -> Result<void> {
    // Capture position + subtree BEFORE the mutation; if remove fails,
    // we discard the capture and the document is untouched.
    const auto* target = doc.find(target_);
    if (target == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveBlockCommand::apply: unknown target " + std::to_string(target_)));
    }
    std::vector<BlockNode> snapshot;
    snapshot_subtree(doc, target_, snapshot);

    const auto [parent_before, index_before] = find_position(doc, target_);

    auto r = doc.remove_block(target_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    snapshot_ = std::move(snapshot);
    old_index_ = index_before;
    (void) parent_before;  // already encoded in snapshot[0].parent
    return {};
}

auto RemoveBlockCommand::undo(Document& doc) -> Result<void> {
    if (snapshot_.empty()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveBlockCommand::undo: command was not applied"));
    }
    // Move the snapshot into the document (restore_subtree takes by value).
    auto local = std::move(snapshot_);
    auto r = doc.restore_subtree(std::move(local), old_index_);
    if (!r) {
        // Restoration failed — re-stash the snapshot so a later retry
        // (or the test author) can inspect it.
        snapshot_ = std::move(local);
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// MoveBlockCommand
// ============================================================================

MoveBlockCommand::MoveBlockCommand(BlockId target, BlockId new_parent, std::size_t new_index)
    : target_(target), new_parent_(new_parent), new_index_(new_index) {}

auto MoveBlockCommand::apply(Document& doc) -> Result<void> {
    const auto [parent_before, index_before] = find_position(doc, target_);
    if (parent_before == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "MoveBlockCommand::apply: target is unknown or has no parent (cannot move the root)"));
    }
    auto r = doc.move_to(target_, new_parent_, new_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_parent_ = parent_before;
    old_index_ = index_before;
    return {};
}

auto MoveBlockCommand::undo(Document& doc) -> Result<void> {
    if (old_parent_ == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "MoveBlockCommand::undo: command was not applied"));
    }
    auto r = doc.move_to(target_, old_parent_, old_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_parent_ = invalid_block_id;
    return {};
}

// ============================================================================
// SetPayloadCommand
// ============================================================================

SetPayloadCommand::SetPayloadCommand(BlockId target, BlockPayload new_payload)
    : target_(target), new_payload_(std::move(new_payload)) {}

auto SetPayloadCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "SetPayloadCommand::apply: unknown target " + std::to_string(target_)));
    }
    BlockPayload before = node->payload;
    auto r = doc.set_payload(target_, new_payload_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_payload_ = std::move(before);
    return {};
}

auto SetPayloadCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_payload(target_, old_payload_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// SetVisibleCommand
// ============================================================================

SetVisibleCommand::SetVisibleCommand(BlockId target, bool visible)
    : target_(target), new_visible_(visible) {}

auto SetVisibleCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "SetVisibleCommand::apply: unknown target " + std::to_string(target_)));
    }
    const bool before = node->visible;
    auto r = doc.set_visible(target_, new_visible_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_visible_ = before;
    return {};
}

auto SetVisibleCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_visible(target_, old_visible_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// SetNameCommand
// ============================================================================

SetNameCommand::SetNameCommand(BlockId target, std::string new_name)
    : target_(target), new_name_(std::move(new_name)) {}

auto SetNameCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SetNameCommand::apply: unknown target " + std::to_string(target_)));
    }
    std::string before = node->name;
    auto r = doc.set_name(target_, new_name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_name_ = std::move(before);
    return {};
}

auto SetNameCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_name(target_, old_name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// AddPageCommand
// ============================================================================

AddPageCommand::AddPageCommand(float w, float h, noted::canvas::PageBackground bg, float origin_x)
    : w_(w), h_(h), bg_(bg), origin_x_(origin_x) {}

auto AddPageCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_page(w_, h_, bg_, origin_x_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    applied_ = true;
    return {};
}

auto AddPageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddPageCommand::undo: command was not applied"));
    }
    auto r = doc.remove_page(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemovePageCommand
// ============================================================================

RemovePageCommand::RemovePageCommand(std::size_t index) : target_index_(index) {}

auto RemovePageCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.pages().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemovePageCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.pages().size()) + ")"));
    }
    // Snapshot BEFORE removal so the inverse has the full state.
    snapshot_ = doc.pages().pages()[target_index_];
    auto r = doc.remove_page(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemovePageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemovePageCommand::undo: command was not applied"));
    }
    auto r = doc.insert_page(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// AddShapeCommand
// ============================================================================

AddShapeCommand::AddShapeCommand(noted::domain::tool::ShapePrimitive shape)
    : shape_(std::move(shape)) {}

auto AddShapeCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_shape(shape_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    applied_ = true;
    return {};
}

auto AddShapeCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddShapeCommand::undo: command was not applied"));
    }
    auto r = doc.remove_shape(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemoveShapeCommand
// ============================================================================

RemoveShapeCommand::RemoveShapeCommand(std::size_t index) : target_index_(index) {}

auto RemoveShapeCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.shapes().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveShapeCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.shapes().size()) + ")"));
    }
    snapshot_ = doc.shapes()[target_index_];
    auto r = doc.remove_shape(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemoveShapeCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveShapeCommand::undo: command was not applied"));
    }
    auto r = doc.insert_shape(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// DeleteShapesCommand
// ============================================================================

DeleteShapesCommand::DeleteShapesCommand(std::vector<std::size_t> indices)
    : target_indices_(std::move(indices)) {
    // Sort + dedup so the apply order (descending) is well-defined
    // and the snapshot list comes back ascending for undo.
    std::sort(target_indices_.begin(), target_indices_.end());
    target_indices_.erase(std::unique(target_indices_.begin(), target_indices_.end()),
                          target_indices_.end());
}

auto DeleteShapesCommand::apply(Document& doc) -> Result<void> {
    if (target_indices_.empty()) {
        applied_ = true;  // no-op succeeds — empty paste should not stall the stack
        return {};
    }
    // Validate first. Refusing partway through would leave the
    // document in a state the user didn't ask for.
    for (auto idx : target_indices_) {
        if (idx >= doc.shapes().size()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                "DeleteShapesCommand::apply: index " + std::to_string(idx) +
                    " out of range (size " + std::to_string(doc.shapes().size()) + ")"));
        }
    }
    // Snapshot in ASC index order (matches the sorted list).
    snapshots_.clear();
    snapshots_.reserve(target_indices_.size());
    for (auto idx : target_indices_) {
        snapshots_.emplace_back(idx, doc.shapes()[idx]);
    }
    // Remove in DESCENDING order so the surviving indices stay
    // stable during the loop.
    for (auto it = target_indices_.rbegin(); it != target_indices_.rend(); ++it) {
        auto r = doc.remove_shape(*it);
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
    }
    applied_ = true;
    return {};
}

auto DeleteShapesCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "DeleteShapesCommand::undo: command was not applied"));
    }
    // Re-insert ascending so each insertion uses the original index
    // — earlier inserts shift later ones, but since we recorded
    // original indices ascending, each subsequent original index
    // is exactly where the next shape needs to land.
    for (const auto& [idx, shape] : snapshots_) {
        auto r = doc.insert_shape(idx, shape);
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
    }
    snapshots_.clear();
    applied_ = false;
    return {};
}

// ============================================================================
// PasteShapesCommand
// ============================================================================

PasteShapesCommand::PasteShapesCommand(std::vector<noted::domain::tool::ShapePrimitive> shapes)
    : shapes_(std::move(shapes)) {}

auto PasteShapesCommand::apply(Document& doc) -> Result<void> {
    if (shapes_.empty()) {
        applied_ = true;
        return {};
    }
    first_assigned_index_ = doc.shapes().size();
    for (const auto& s : shapes_) {
        auto r = doc.add_shape(s);
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
    }
    applied_ = true;
    return {};
}

auto PasteShapesCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "PasteShapesCommand::undo: command was not applied"));
    }
    // Remove the same number we added, from the end. Append-only
    // semantics on apply means the last `shapes_.size()` indices
    // are guaranteed to be ours.
    for (std::size_t i = 0; i < shapes_.size(); ++i) {
        const std::size_t idx = doc.shapes().size() - 1;
        auto r = doc.remove_shape(idx);
        if (!r) {
            return std::unexpected(std::move(r).error());
        }
    }
    applied_ = false;
    return {};
}

// ============================================================================
// AddTextCommand
// ============================================================================

AddTextCommand::AddTextCommand(noted::domain::tool::TextPrimitive text) : text_(std::move(text)) {}

auto AddTextCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_text(text_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    applied_ = true;
    return {};
}

auto AddTextCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddTextCommand::undo: command was not applied"));
    }
    auto r = doc.remove_text(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemoveTextCommand
// ============================================================================

RemoveTextCommand::RemoveTextCommand(std::size_t index) : target_index_(index) {}

auto RemoveTextCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.texts().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveTextCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.texts().size()) + ")"));
    }
    snapshot_ = doc.texts()[target_index_];
    auto r = doc.remove_text(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemoveTextCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveTextCommand::undo: command was not applied"));
    }
    auto r = doc.insert_text(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// AddImageCommand
// ============================================================================

AddImageCommand::AddImageCommand(noted::domain::tool::ImagePrimitive image)
    : image_(std::move(image)) {}

auto AddImageCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_image(image_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    applied_ = true;
    return {};
}

auto AddImageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddImageCommand::undo: command was not applied"));
    }
    auto r = doc.remove_image(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemoveImageCommand
// ============================================================================

RemoveImageCommand::RemoveImageCommand(std::size_t index) : target_index_(index) {}

auto RemoveImageCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.images().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveImageCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.images().size()) + ")"));
    }
    snapshot_ = doc.images()[target_index_];
    auto r = doc.remove_image(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemoveImageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveImageCommand::undo: command was not applied"));
    }
    auto r = doc.insert_image(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// AddStrokeCommand
// ============================================================================

AddStrokeCommand::AddStrokeCommand(noted::stroke::Stroke stroke) : stroke_(std::move(stroke)) {}

auto AddStrokeCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_stroke(stroke_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    // Capture the layer_id the document stamped onto the stroke so a
    // subsequent undo → redo cycle re-applies the exact same layer
    // (rather than re-running the lazy-active fallback against a
    // possibly-different active layer at redo time).
    stroke_.layer_id = doc.strokes()[assigned_index_].layer_id;
    applied_ = true;
    return {};
}

auto AddStrokeCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "AddStrokeCommand::undo: command was not applied"));
    }
    auto r = doc.remove_stroke(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemoveStrokeCommand
// ============================================================================

RemoveStrokeCommand::RemoveStrokeCommand(std::size_t index) : target_index_(index) {}

auto RemoveStrokeCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.strokes().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveStrokeCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.strokes().size()) + ")"));
    }
    snapshot_ = doc.strokes()[target_index_];
    auto r = doc.remove_stroke(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemoveStrokeCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveStrokeCommand::undo: command was not applied"));
    }
    auto r = doc.insert_stroke(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// AddCanvasLayerCommand
// ============================================================================

AddCanvasLayerCommand::AddCanvasLayerCommand(std::string name) : name_(std::move(name)) {}

auto AddCanvasLayerCommand::apply(Document& doc) -> Result<void> {
    previous_active_ = doc.active_layer();
    auto id = doc.add_canvas_layer(name_);
    if (!id) {
        return std::unexpected(std::move(id).error());
    }
    assigned_id_ = *id;
    // Promote to active so paint lands on the freshly-added layer —
    // matches the layer panel's "Add Layer doubles as switch to it"
    // UX. We do this even if the stack had a different active layer
    // before (which `add_canvas_layer` would otherwise preserve).
    if (auto r = doc.set_active_layer(assigned_id_); !r) {
        // Roll back the add — apply() must leave the doc untouched on
        // failure (validate-then-mutate). add_canvas_layer succeeded
        // so the layer IS in the stack; the only failure mode for
        // set_active is the id not resolving, which can't happen
        // here. Defensive nonetheless.
        const auto removed = doc.remove_canvas_layer(doc.canvas_layers().size() - 1U);
        (void) removed;  // best-effort rollback
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto AddCanvasLayerCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "AddCanvasLayerCommand::undo: command was not applied"));
    }
    // Find the layer by id (its index may have moved if other layer
    // commands ran in between — though in practice the UndoStack
    // serializes execution).
    const auto& layers = doc.canvas_layers().layers();
    std::size_t idx = layers.size();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].id == assigned_id_) {
            idx = i;
            break;
        }
    }
    if (idx == layers.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddCanvasLayerCommand::undo: layer id " +
                                                     std::to_string(assigned_id_) +
                                                     " not found — stack mutated externally?"));
    }
    auto removed = doc.remove_canvas_layer(idx);
    if (!removed) {
        return std::unexpected(std::move(removed).error());
    }
    // Restore the previous active id. If it pointed at a layer that
    // has since been removed, set_active_layer rejects — clear to
    // invalid in that case rather than failing the undo.
    if (previous_active_ != noted::invalid_layer_id) {
        if (auto r = doc.set_active_layer(previous_active_); !r) {
            (void) doc.set_active_layer(noted::invalid_layer_id);
        }
    } else {
        (void) doc.set_active_layer(noted::invalid_layer_id);
    }
    applied_ = false;
    return {};
}

// ============================================================================
// DuplicateCanvasLayerCommand
// ============================================================================

DuplicateCanvasLayerCommand::DuplicateCanvasLayerCommand(std::size_t source_index,
                                                         std::string new_name)
    : source_index_(source_index), new_name_(std::move(new_name)) {}

auto DuplicateCanvasLayerCommand::apply(Document& doc) -> Result<void> {
    if (source_index_ >= doc.canvas_layers().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "DuplicateCanvasLayerCommand::apply: source index " + std::to_string(source_index_) +
                " out of range (size " + std::to_string(doc.canvas_layers().size()) + ")"));
    }
    previous_active_ = doc.active_layer();
    const auto source_id = doc.canvas_layers().layers()[source_index_].id;
    // Default name: "<source name> copy" matches Photoshop's
    // duplicate convention; the caller can override.
    std::string name = new_name_;
    if (name.empty()) {
        name = doc.canvas_layers().layers()[source_index_].name + " copy";
    }
    auto new_id_r = doc.duplicate_canvas_layer(source_index_, std::move(name));
    if (!new_id_r) {
        return std::unexpected(std::move(new_id_r).error());
    }
    assigned_id_ = *new_id_r;

    // Clone every stroke whose layer_id matches the source onto the
    // new layer. Snapshot the current strokes vector first so we
    // don't iterate over our own appends.
    first_added_stroke_index_ = doc.strokes().size();
    assigned_strokes_count_ = 0;
    const auto& strokes = doc.strokes();
    const std::size_t snapshot_size = strokes.size();
    for (std::size_t i = 0; i < snapshot_size; ++i) {
        if (strokes[i].layer_id != source_id) {
            continue;
        }
        noted::stroke::Stroke clone = strokes[i];
        clone.layer_id = assigned_id_;
        auto added = doc.add_stroke(std::move(clone));
        if (!added) {
            // Roll back partial appends + the new layer to keep
            // apply atomic.
            const auto added_so_far = doc.strokes().size() - first_added_stroke_index_;
            for (std::size_t k = 0; k < added_so_far; ++k) {
                (void) doc.remove_stroke(doc.strokes().size() - 1U);
            }
            // The duplicate layer is at source_index_ + 1 (it was
            // moved there in duplicate_canvas_layer).
            (void) doc.remove_canvas_layer(source_index_ + 1U);
            return std::unexpected(std::move(added).error());
        }
        ++assigned_strokes_count_;
    }
    // Promote the duplicate to active — matches the "Add layer"
    // command convention so the user can paint on the clone right
    // away.
    (void) doc.set_active_layer(assigned_id_);
    applied_ = true;
    return {};
}

auto DuplicateCanvasLayerCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "DuplicateCanvasLayerCommand::undo: command was not applied"));
    }
    // Remove the cloned strokes from the tail (they were appended
    // in apply via add_stroke, so they live at indices
    // [first_added_stroke_index_, first_added_stroke_index_ +
    // assigned_strokes_count_)).
    for (std::size_t k = 0; k < assigned_strokes_count_; ++k) {
        if (auto r = doc.remove_stroke(doc.strokes().size() - 1U); !r) {
            return std::unexpected(std::move(r).error());
        }
    }
    // Find the duplicate layer by id (its index may have moved if
    // other layer commands ran in between).
    const auto& layers = doc.canvas_layers().layers();
    std::size_t idx = layers.size();
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].id == assigned_id_) {
            idx = i;
            break;
        }
    }
    if (idx == layers.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "DuplicateCanvasLayerCommand::undo: layer id " +
                                                     std::to_string(assigned_id_) +
                                                     " not found — stack mutated externally?"));
    }
    auto removed = doc.remove_canvas_layer(idx);
    if (!removed) {
        return std::unexpected(std::move(removed).error());
    }
    if (previous_active_ != noted::invalid_layer_id) {
        if (auto r = doc.set_active_layer(previous_active_); !r) {
            (void) doc.set_active_layer(noted::invalid_layer_id);
        }
    } else {
        (void) doc.set_active_layer(noted::invalid_layer_id);
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemoveCanvasLayerCommand
// ============================================================================

RemoveCanvasLayerCommand::RemoveCanvasLayerCommand(std::size_t index) : target_index_(index) {}

auto RemoveCanvasLayerCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.canvas_layers().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveCanvasLayerCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.canvas_layers().size()) + ")"));
    }
    previous_active_ = doc.active_layer();
    snapshot_ = doc.canvas_layers().layers()[target_index_];
    auto removed = doc.remove_canvas_layer(target_index_);
    if (!removed) {
        return std::unexpected(std::move(removed).error());
    }
    applied_ = true;
    return {};
}

auto RemoveCanvasLayerCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "RemoveCanvasLayerCommand::undo: command was not applied"));
    }
    // Restore the layer at its original index via the stack's
    // bypass-path inverse. We don't have a public `insert_layer` on
    // Document but `CanvasLayerStack::insert_layer` (visible via
    // replace_canvas_layers) reseats it. Build the new stack by
    // copying current + inserting snapshot at target_index_.
    auto stack = doc.canvas_layers();
    if (auto r = stack.insert_layer(target_index_, snapshot_); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Restore the previous active id (which may have been the
    // removed layer itself, or a sibling — either way it should be
    // valid against the reseated stack).
    doc.replace_canvas_layers(std::move(stack), previous_active_);
    applied_ = false;
    return {};
}

// ============================================================================
// MoveCanvasLayerCommand
// ============================================================================

MoveCanvasLayerCommand::MoveCanvasLayerCommand(std::size_t from_index, std::size_t to_index)
    : from_index_(from_index), to_index_(to_index) {}

auto MoveCanvasLayerCommand::apply(Document& doc) -> Result<void> {
    const auto stack_size = doc.canvas_layers().size();
    if (from_index_ >= stack_size || to_index_ >= stack_size) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "MoveCanvasLayerCommand::apply: index out of range (size " +
                std::to_string(stack_size) + ", from " + std::to_string(from_index_) + ", to " +
                std::to_string(to_index_) + ")"));
    }
    if (from_index_ == to_index_) {
        // No-op move. Still mark applied so undo (also a no-op) is
        // consistent with the post-apply state.
        applied_ = true;
        return {};
    }
    if (auto r = doc.move_canvas_layer(from_index_, to_index_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto MoveCanvasLayerCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "MoveCanvasLayerCommand::undo: command was not applied"));
    }
    if (from_index_ == to_index_) {
        applied_ = false;
        return {};
    }
    // Inverse: move the layer back. After `move(from, to)`, the
    // layer that was at `from` now lives at `to`. Move it back via
    // `move(to, from)`.
    if (auto r = doc.move_canvas_layer(to_index_, from_index_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// SetLayerVisibleCommand
// ============================================================================

SetLayerVisibleCommand::SetLayerVisibleCommand(noted::LayerId target, bool new_value)
    : target_(target), new_value_(new_value) {}

auto SetLayerVisibleCommand::apply(Document& doc) -> Result<void> {
    const auto* layer = doc.canvas_layers().find(target_);
    if (layer == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "SetLayerVisibleCommand::apply: id " +
                                                     std::to_string(target_) +
                                                     " not in canvas layer stack"));
    }
    old_value_ = layer->visible;
    if (auto r = doc.set_layer_visible(target_, new_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto SetLayerVisibleCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "SetLayerVisibleCommand::undo: command was not applied"));
    }
    if (auto r = doc.set_layer_visible(target_, old_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// SetLayerLockedCommand
// ============================================================================

SetLayerLockedCommand::SetLayerLockedCommand(noted::LayerId target, bool new_value)
    : target_(target), new_value_(new_value) {}

auto SetLayerLockedCommand::apply(Document& doc) -> Result<void> {
    const auto* layer = doc.canvas_layers().find(target_);
    if (layer == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "SetLayerLockedCommand::apply: id " +
                                                     std::to_string(target_) +
                                                     " not in canvas layer stack"));
    }
    old_value_ = layer->locked;
    if (auto r = doc.set_layer_locked(target_, new_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto SetLayerLockedCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "SetLayerLockedCommand::undo: command was not applied"));
    }
    if (auto r = doc.set_layer_locked(target_, old_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// SetLayerNameCommand
// ============================================================================

SetLayerNameCommand::SetLayerNameCommand(noted::LayerId target, std::string new_name)
    : target_(target), new_name_(std::move(new_name)) {}

auto SetLayerNameCommand::apply(Document& doc) -> Result<void> {
    const auto* layer = doc.canvas_layers().find(target_);
    if (layer == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "SetLayerNameCommand::apply: id " +
                                                     std::to_string(target_) +
                                                     " not in canvas layer stack"));
    }
    old_name_ = layer->name;
    if (auto r = doc.set_layer_name(target_, new_name_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto SetLayerNameCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "SetLayerNameCommand::undo: command was not applied"));
    }
    if (auto r = doc.set_layer_name(target_, old_name_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

auto SetLayerNameCommand::try_merge(const Command& newer) noexcept -> bool {
    // Only coalesce successive renames on the same layer. Keystroke
    // runs in `ImGui::InputText` produce a flurry of commits if the
    // host pipes every text-change through here; one history entry
    // covers the whole gesture.
    const auto* other = dynamic_cast<const SetLayerNameCommand*>(&newer);
    if (other == nullptr || other->target_ != target_) {
        return false;
    }
    new_name_ = other->new_name_;
    return true;
}

// ============================================================================
// SetLayerOpacityCommand
// ============================================================================

SetLayerOpacityCommand::SetLayerOpacityCommand(noted::LayerId target, float new_value)
    : target_(target), new_value_(new_value) {}

auto SetLayerOpacityCommand::apply(Document& doc) -> Result<void> {
    const auto* layer = doc.canvas_layers().find(target_);
    if (layer == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "SetLayerOpacityCommand::apply: id " +
                                                     std::to_string(target_) +
                                                     " not in canvas layer stack"));
    }
    old_value_ = layer->opacity;
    // Document::set_layer_opacity → CanvasLayerStack::set_opacity
    // clamps to [0, 1] internally; we store the clamped value as
    // `new_value_` so a future redo replays exactly what apply
    // produced (not the unclamped caller-supplied value).
    if (auto r = doc.set_layer_opacity(target_, new_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (const auto* l = doc.canvas_layers().find(target_); l != nullptr) {
        new_value_ = l->opacity;
    }
    applied_ = true;
    return {};
}

auto SetLayerOpacityCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "SetLayerOpacityCommand::undo: command was not applied"));
    }
    if (auto r = doc.set_layer_opacity(target_, old_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

auto SetLayerOpacityCommand::try_merge(const Command& newer) noexcept -> bool {
    // Same-target opacity sliders coalesce. We DON'T touch
    // `old_value_` (the pre-drag opacity) so a single undo rewinds
    // the entire drag.
    const auto* other = dynamic_cast<const SetLayerOpacityCommand*>(&newer);
    if (other == nullptr || other->target_ != target_) {
        return false;
    }
    new_value_ = other->new_value_;
    return true;
}

// ============================================================================
// SetLayerBlendCommand
// ============================================================================

SetLayerBlendCommand::SetLayerBlendCommand(noted::LayerId target,
                                           noted::domain::BlendMode new_value)
    : target_(target), new_value_(new_value) {}

auto SetLayerBlendCommand::apply(Document& doc) -> Result<void> {
    const auto* layer = doc.canvas_layers().find(target_);
    if (layer == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "SetLayerBlendCommand::apply: id " +
                                                     std::to_string(target_) +
                                                     " not in canvas layer stack"));
    }
    old_value_ = layer->blend;
    if (auto r = doc.set_layer_blend(target_, new_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto SetLayerBlendCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "SetLayerBlendCommand::undo: command was not applied"));
    }
    if (auto r = doc.set_layer_blend(target_, old_value_); !r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// UndoStack
// ============================================================================

auto UndoStack::execute(std::unique_ptr<Command> cmd, Document& doc) -> Result<void> {
    if (cmd == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "UndoStack::execute: cmd is null"));
    }
    auto r = cmd->apply(doc);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    // Always clear redo BEFORE the merge check — a new edit (even one
    // that ends up coalesced into the prior entry) invalidates any
    // outstanding redo path. Otherwise a slider drag right after an
    // undo could "redo" a stale state.
    redo_stack_.clear();
    // Coalescing: ask the current top of stack whether it can absorb
    // the new command. Only the immediate top is consulted, so a
    // brief pause that lets some other command settle in between will
    // correctly break a drag into two undo entries.
    if (!undo_stack_.empty() && undo_stack_.back()->try_merge(*cmd)) {
        // Absorbed — destroy the newer command (already applied; its
        // "after" value has been folded into the existing top). Stack
        // size unchanged, dirty proxy preserved (the original push
        // already moved undo_size off saved_undo_size).
        return {};
    }
    undo_stack_.push_back(std::move(cmd));
    // Trim oldest entries beyond the depth bound. erase-front is O(N)
    // but N is bounded by max_depth_; for a 200-deep stack this is
    // cheaper than the linked-list alternative we'd otherwise reach for.
    if (undo_stack_.size() > max_depth_) {
        const auto excess = undo_stack_.size() - max_depth_;
        undo_stack_.erase(undo_stack_.begin(),
                          undo_stack_.begin() + static_cast<std::ptrdiff_t>(excess));
    }
    return {};
}

auto UndoStack::undo(Document& doc) -> Result<void> {
    if (undo_stack_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state, "UndoStack::undo: nothing to undo"));
    }
    auto cmd = std::move(undo_stack_.back());
    auto r = cmd->undo(doc);
    if (!r) {
        // Inverse failed — re-stash and surface the error. The document
        // is now in an inconsistent state (a real bug, not user error);
        // the caller is responsible for deciding whether to keep going.
        undo_stack_.back() = std::move(cmd);
        return std::unexpected(std::move(r).error());
    }
    undo_stack_.pop_back();
    redo_stack_.push_back(std::move(cmd));
    return {};
}

auto UndoStack::redo(Document& doc) -> Result<void> {
    if (redo_stack_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state, "UndoStack::redo: nothing to redo"));
    }
    auto cmd = std::move(redo_stack_.back());
    auto r = cmd->apply(doc);
    if (!r) {
        redo_stack_.back() = std::move(cmd);
        return std::unexpected(std::move(r).error());
    }
    redo_stack_.pop_back();
    undo_stack_.push_back(std::move(cmd));
    return {};
}

void UndoStack::clear() noexcept {
    undo_stack_.clear();
    redo_stack_.clear();
}

auto UndoStack::peek_undo() const noexcept -> const Command* {
    return undo_stack_.empty() ? nullptr : undo_stack_.back().get();
}

auto UndoStack::peek_redo() const noexcept -> const Command* {
    return redo_stack_.empty() ? nullptr : redo_stack_.back().get();
}

}  // namespace noted::domain
