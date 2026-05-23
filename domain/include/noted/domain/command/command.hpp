#pragma once

// Command — single, reversible state mutation on a `Document`.
//
// Every user-visible edit (add/remove/move/set_payload/set_visible/
// set_name/insert) is encoded as a concrete `Command` subclass. The
// `UndoStack` (see undo_stack.hpp) owns the stack of applied commands;
// `undo()` calls `Command::undo()` to rewind, `redo()` re-applies.
//
// Why pass `Document&` to apply/undo instead of holding a Document
// pointer in the command:
//   - Commands have to replay on a **different** `Document` instance —
//     CRDT replication ships them across machines, the file format
//     replays them on load to rebuild history, undo stacks survive
//     across document reopens. A bound pointer would dangle on every
//     one of those cases.
//   - Tests construct a fresh Document per case and run commands
//     against it. Zero plumbing.
//
// Atomicity contract:
//   - `apply()` must leave the document either fully mutated (success)
//     OR untouched (any Result::error). The undo stack only retains
//     commands that returned success.
//   - `undo()` must be the precise inverse of a successful `apply()`
//     **on the same Document state** — i.e. apply(d) → undo(d) returns
//     d to its prior state.
//
// Concrete commands are declared in commands.hpp (one header that
// includes them all). This base file stays minimal so consumers that
// only need the abstract interface can include it cheaply.
//
// Rationale: see docs/architecture/0024-command-undo-redo.md.

#include <string>

#include "noted/engine/error/error.hpp"

namespace noted::domain {

class Document;  // forward — full include only needed by concrete commands

class Command {
public:
    Command() = default;
    Command(const Command&) = delete;
    auto operator=(const Command&) -> Command& = delete;
    Command(Command&&) = delete;
    auto operator=(Command&&) -> Command& = delete;
    virtual ~Command() = default;

    // Apply the change. On success, internal state is captured so a
    // subsequent `undo(doc)` can reverse it. On failure, the document
    // and the command itself are untouched.
    [[nodiscard]] virtual auto apply(Document& doc) -> Result<void> = 0;

    // Reverse a previous successful `apply(doc)`. Must be called on a
    // document that is in the post-apply state. Returns an error only
    // for internal inconsistencies (e.g. corrupted document, bug); a
    // well-formed command on a well-formed document never fails.
    [[nodiscard]] virtual auto undo(Document& doc) -> Result<void> = 0;

    // Short human-readable label, e.g. "Add block", "Move block".
    // Used by the UI's undo menu. Stable across versions.
    [[nodiscard]] virtual auto label() const noexcept -> std::string_view = 0;

    // Coalescing hook for high-frequency edits (slider drags, rename
    // keystrokes). Called by `UndoStack::execute` BEFORE pushing a new
    // command onto the undo stack: the stack offers the new command to
    // the current top via `top->try_merge(*newer)`. If `try_merge`
    // returns `true`, the stack treats the newer command as absorbed
    // (it is destroyed without being pushed) and the existing top is
    // assumed to have updated its "new value" in place — so a 60-Hz
    // opacity slider produces ONE undo entry, not 60.
    //
    // Contract:
    //   - The newer command has **already been successfully applied**
    //     when this is called. The merge therefore must not re-apply
    //     anything; it only updates the receiver's stored target
    //     value so that the receiver's `undo` still rewinds to the
    //     original "before" state, and a subsequent redo replays to
    //     the latest "after" state.
    //   - Returning `true` means "I absorbed this; throw it away."
    //     Returning `false` means "push as a separate entry."
    //   - Default returns `false` — most commands are atomic and
    //     should not coalesce.
    //   - Only the top of the undo stack is consulted; coalescing
    //     never bridges across an unrelated entry. This means a brief
    //     pause (UI re-renders some other command in between) is
    //     enough to break a drag into two undo entries — desirable.
    //
    // Implementations should be conservative: same kind, same target
    // identity (e.g. layer id, block id), and a class-specific
    // "looks like the same continuous gesture" check. When in doubt,
    // return `false` — over-merging hides user intent in history.
    [[nodiscard]] virtual auto try_merge(const Command& /*newer*/) noexcept -> bool {
        return false;
    }
};

}  // namespace noted::domain
