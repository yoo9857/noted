#pragma once

// Document + UndoStack + on-disk identity, bundled as one unit so
// every part of the app that touches "the current document" routes
// through a single owner.
//
// Holds:
//   - `document`      — the live `Document` tree
//   - `undo_stack`    — `UndoStack` over that document
//   - `selected_block` — outline panel's current selection
//   - `current_path`  — `.noted` backing path, nullopt for untitled
//   - `saved_undo_size` — `undo_stack.undo_size()` snapshot at the
//     last successful save; comparing it to the current size yields
//     `is_dirty()`. Same proxy that PR #45 introduced — close
//     enough for v0.x; a true dirty bit would diff `Document` state.
//
// Operations preserve the invariants:
//   - `save_to(path)` writes the archive, then updates
//     `current_path` and snapshots `saved_undo_size`.
//   - `open_from(path)` loads, replaces the document, clears the
//     undo stack, resets selection, marks the new state as saved.
//   - `reset_to_blank()` does the same as opening an empty doc.
//
// Pure state — no GLFW, no ImGui. The host queries `title()` per
// frame and decides when to call `glfwSetWindowTitle`.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "noted/domain/command/undo_stack.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain {
class Command;
}  // namespace noted::domain

namespace noted::app {

class DocumentSession {
public:
    DocumentSession() = default;

    DocumentSession(const DocumentSession&) = delete;
    auto operator=(const DocumentSession&) -> DocumentSession& = delete;
    DocumentSession(DocumentSession&&) noexcept = default;
    auto operator=(DocumentSession&&) noexcept -> DocumentSession& = default;
    ~DocumentSession() = default;

    // Read access for everything that needs to walk the doc tree —
    // outline panel, status displays, command construction.
    [[nodiscard]] auto document() noexcept -> noted::domain::Document& { return document_; }
    [[nodiscard]] auto document() const noexcept -> const noted::domain::Document& {
        return document_;
    }
    [[nodiscard]] auto undo_stack() noexcept -> noted::domain::UndoStack& { return undo_stack_; }
    [[nodiscard]] auto undo_stack() const noexcept -> const noted::domain::UndoStack& {
        return undo_stack_;
    }

    [[nodiscard]] auto selected_block() const noexcept -> noted::domain::BlockId {
        return selected_block_;
    }
    void set_selected_block(noted::domain::BlockId id) noexcept { selected_block_ = id; }

    [[nodiscard]] auto current_path() const noexcept
        -> const std::optional<std::filesystem::path>& {
        return current_path_;
    }
    [[nodiscard]] auto has_path() const noexcept -> bool { return current_path_.has_value(); }

    [[nodiscard]] auto is_dirty() const noexcept -> bool {
        return undo_stack_.undo_size() != saved_undo_size_;
    }

    // "noted — <filename> [*]". The trailing star marks dirty.
    [[nodiscard]] auto title() const -> std::string;

    // Apply an inverse-based command via the undo stack, refreshing
    // the dirty baseline implicitly (saved_undo_size_ stays at its
    // previous value so the new edit reads as dirty).
    [[nodiscard]] auto execute(std::unique_ptr<noted::domain::Command> cmd) -> Result<void>;
    [[nodiscard]] auto undo() -> Result<void>;
    [[nodiscard]] auto redo() -> Result<void>;

    // Replace the document with an empty one, clear the undo stack,
    // forget the on-disk path, and mark the result as saved (no
    // pending changes to discard).
    void reset_to_blank();

    // Write the document to `path`. On success, `current_path` is
    // set and the dirty baseline snapshots to the current undo size.
    [[nodiscard]] auto save_to(const std::filesystem::path& path) -> Result<void>;

    // Load `path` into a fresh document. On success, replaces every
    // field; the loaded state reads as not-dirty.
    [[nodiscard]] auto open_from(const std::filesystem::path& path) -> Result<void>;

private:
    noted::domain::Document document_{};
    noted::domain::UndoStack undo_stack_{};
    noted::domain::BlockId selected_block_{noted::domain::invalid_block_id};
    std::optional<std::filesystem::path> current_path_{};
    std::size_t saved_undo_size_{0};
};

}  // namespace noted::app
