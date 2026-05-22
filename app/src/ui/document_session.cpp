#include "ui/document_session.hpp"

#include <utility>

#include "noted/domain/command/command.hpp"
#include "noted/platform/io/noted_file.hpp"

namespace noted::app {

namespace {

// Ensure the document has at least one canvas layer + a valid active
// one — the user experience contract is that there's ALWAYS a paint
// surface ready (matches Photoshop's "every new document opens with
// 'Background' selected"). Direct-write rather than via Command so
// the seed doesn't pollute undo history or mark the document dirty.
void ensure_default_canvas_layer(noted::domain::Document& doc) {
    if (doc.canvas_layers().empty()) {
        (void) doc.add_canvas_layer("Layer 1");
    } else if (doc.active_layer() == noted::invalid_layer_id) {
        // Loaded a v8 file whose stack was non-empty but `active` was 0
        // (or somehow drifted). Pick the top of the stack so the next
        // stroke lands on a real layer.
        const auto top = doc.canvas_layers().layers().back().id;
        (void) doc.set_active_layer(top);
    }
}

}  // namespace

auto DocumentSession::title() const -> std::string {
    const std::string name =
        current_path_.has_value() ? current_path_->filename().string() : std::string{"Untitled"};
    const bool dirty = is_dirty();
    return std::string{"noted — "} + name + (dirty ? " *" : "");
}

auto DocumentSession::execute(std::unique_ptr<noted::domain::Command> cmd) -> Result<void> {
    return undo_stack_.execute(std::move(cmd), document_);
}

auto DocumentSession::undo() -> Result<void> {
    return undo_stack_.undo(document_);
}

auto DocumentSession::redo() -> Result<void> {
    return undo_stack_.redo(document_);
}

void DocumentSession::reset_to_blank() {
    document_ = noted::domain::Document{};
    undo_stack_.clear();
    selected_block_ = noted::domain::invalid_block_id;
    current_path_.reset();
    ensure_default_canvas_layer(document_);
    saved_undo_size_ = undo_stack_.undo_size();
}

auto DocumentSession::save_to(const std::filesystem::path& path) -> Result<void> {
    if (auto r = noted::platform::io::save_noted_file(path, document_); !r) {
        return std::unexpected(std::move(r).error());
    }
    current_path_ = path;
    saved_undo_size_ = undo_stack_.undo_size();
    return {};
}

auto DocumentSession::open_from(const std::filesystem::path& path) -> Result<void> {
    auto loaded = noted::platform::io::load_noted_file(path);
    if (!loaded) {
        return std::unexpected(std::move(loaded).error());
    }
    document_ = std::move(*loaded);
    undo_stack_.clear();
    selected_block_ = noted::domain::invalid_block_id;
    current_path_ = path;
    // Loaded documents from v7 / older — or hand-built v8 files with
    // an empty stack — still need a default layer so the user can
    // start painting immediately after Open. Same direct-write
    // rationale as `reset_to_blank`.
    ensure_default_canvas_layer(document_);
    saved_undo_size_ = undo_stack_.undo_size();
    return {};
}

}  // namespace noted::app
