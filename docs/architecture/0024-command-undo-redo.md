# ADR 0024: Command + undo / redo on `Document`

**Status:** Accepted
**Date:** 2026-05-18

## Context

`domain::Document` (ADR 0023) is the unified data model. Every
non-trivial product feature wants to mutate it: typing, drawing,
inserting an image, dragging a block, toggling visibility. The
moment a user makes a second edit, they expect **undo / redo** —
it's the table-stakes interaction for any editor.

The pieces needed:

1. A **Command** abstraction that encodes one user-visible mutation
   plus its inverse.
2. An **UndoStack** that orchestrates the two stacks (applied /
   undone) with the standard semantics (new edit clears redo,
   bounded depth, peek for UI labels).
3. Enough surface on `Document` itself for commands to express their
   inverses precisely.

This ADR pins the design choices that ripple through every feature
PR after this — file format (commands serialize one way), CRDT
replication (commands are the wire format), keyboard shortcuts,
plugin API, scripting.

## Decision

### Command interface

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual auto apply(Document& doc) -> Result<void> = 0;
    virtual auto undo(Document& doc) -> Result<void> = 0;
    virtual auto label() const noexcept -> std::string_view = 0;

    // Non-copyable, non-movable — commands live in unique_ptr.
};
```

Key shape points:

- **`apply` and `undo` take `Document&`.** A command does NOT hold a
  pointer to a Document. Why: the same command must replay on a
  *different* Document instance — CRDT replicas, file-format
  replay-from-history, undo stacks surviving document reopens. A
  bound pointer would dangle on every one of those use cases.
- **`Result<T>` everywhere.** No exceptions. Failure leaves the
  document untouched (apply contract) or signals a bug (undo
  contract — well-formed inverse of a successful apply never fails).
- **Non-copyable, non-movable** — commands are reference-stable
  while the UndoStack owns them via `unique_ptr`.
- **`label()` returns `string_view`.** Concrete commands return
  static strings; the UI shows them in the Edit menu ("Undo: Add
  block"). The const-correctness lets us call it on a `const
  Command*` returned from `peek_undo()`.

### Inverse-based undo, not snapshot

Two designs were on the table:

| Strategy | Apply cost | Undo cost | Memory per command | Notes |
|---|---|---|---|---|
| **Snapshot** | full Document copy | move-assign | O(document size) | Trivial code |
| **Inverse** (chosen) | one mutation + capture before-state | one inverse mutation | O(change size) | Slightly more code, dramatically less memory |

The chosen path: each concrete command stores only the data needed
to perform its specific inverse. `SetVisibleCommand` stores an old
+ new bool. `MoveBlockCommand` stores old `(parent, index)` after
apply. `RemoveBlockCommand` is the most expensive — it snapshots
the removed subtree — but that's bounded by the size of the
subtree, not the whole document.

Worst case for a 200-deep stack: ~200 × (most commands < 100 B) +
a few KB for any subtree-remove commands. Easy to keep under 1 MB
even on huge documents. The snapshot approach would be ~200 ×
(MB-scale document) — hundreds of megabytes, easily.

### `Document::restore_subtree` — the precise inverse of `remove_block`

For `RemoveBlockCommand` to be a true inverse, `Document` needs a
method that puts a removed subtree back **with its original IDs**
in its original parent + index. We add:

```cpp
auto Document::restore_subtree(std::vector<BlockNode> subtree,
                               std::size_t insert_index) -> Result<void>;
```

Validation pass before any mutation:
- Subtree non-empty.
- Every id is currently absent from the document and unique within
  the subtree.
- Non-root nodes have a parent that appears earlier in the list
  (= internally consistent topology).
- Children references inside the subtree are consistent.

Two cases for the root of the subtree:
- **Normal case:** `subtree[0].parent` names an existing block;
  root is inserted at `insert_index` in that parent's children.
- **Document-root case:** `subtree[0].parent == invalid_block_id`
  and the document currently has no root (= the previous
  `remove_block` removed a childless root). The restored node
  becomes the document root; `insert_index` is ignored.

`next_id_` is advanced past every restored id so subsequent
allocations stay monotonic — preserves the invariant that history
references in other artifacts (CRDT logs, file format)
remain distinct after restore.

This method is **the only new public Document API** in this PR.
Every other command uses pre-existing mutators.

### UndoStack semantics

```cpp
class UndoStack {
public:
    static constexpr std::size_t kDefaultMaxDepth = 200;

    explicit UndoStack(std::size_t max_depth = kDefaultMaxDepth);

    auto execute(std::unique_ptr<Command>, Document&) -> Result<void>;
    auto undo(Document&) -> Result<void>;
    auto redo(Document&) -> Result<void>;
    void clear() noexcept;

    bool can_undo / can_redo / undo_size / redo_size;
    const Command* peek_undo / peek_redo;
};
```

State-changing operations:

- **`execute(cmd, doc)`** applies the command; on success pushes it
  onto `undo_stack_` and clears `redo_stack_` (a new edit
  invalidates any redo path — standard editor semantics). On
  failure, the command is *not* pushed and the document is
  untouched.
- **`undo(doc)`** pops from `undo_stack_`, calls `cmd->undo(doc)`;
  on success pushes onto `redo_stack_`. On undo failure the command
  is **re-stashed** on `undo_stack_` so the bug is visible in a
  debugger — failures here mean either the command's inverse is
  wrong or the document was mutated out-of-band.
- **`redo(doc)`** symmetric.
- **`clear()`** drops both stacks (e.g. on document close).

### Bounded depth

`max_depth_` defaults to 200 (typical IDE / text-editor undo
depth). When `execute` would push the (depth+1)-th command, the
oldest entry is dropped via `vector::erase(begin)`. O(N) but
N ≤ 200; cheaper than a deque or linked-list at this scale,
matches the "no premature data-structure complexity" rule.

The depth cap is configurable. `0` is rewritten to the default in
the constructor — a zero-depth stack is almost certainly a bug.

### Atomicity rules

| Operation | On success | On failure |
|---|---|---|
| `execute` | Push cmd to undo; clear redo; trim if over depth. | Don't push. Document untouched (apply contract). |
| `undo` | Pop from undo; push to redo. | Re-stash on undo. Surface error. |
| `redo` | Pop from redo; push to undo. | Re-stash on redo. Surface error. |

The "re-stash on failure" pattern lets a debugger see exactly what
the broken command was. In well-formed code these paths are
unreachable; defending them costs almost nothing.

### Why these 7 commands and not more / fewer

The 7 concrete commands map 1:1 to `Document`'s public mutator
surface:

| Command | Inverse strategy |
|---|---|
| `AddBlockCommand` | Stores assigned id; undo = remove. |
| `InsertBlockCommand` | Same as Add. |
| `RemoveBlockCommand` | Snapshots subtree; undo = restore_subtree. |
| `MoveBlockCommand` | Stores old (parent, index); undo = move back. |
| `SetPayloadCommand` | Stores old payload; undo = set old payload. |
| `SetVisibleCommand` | Stores old bool. |
| `SetNameCommand` | Stores old string. |

1:1 with `Document`'s surface means there's a clear rule for
introducing a new mutator: every public Document mutator gets a
matching Command. No surprises, no "command for something the
document can't even do natively."

## Alternatives considered

- **Snapshot-per-command undo.** Wasteful at any non-trivial
  document size. See table above. Rejected.
- **Single sum-type command (no virtual dispatch).** A
  `std::variant<AddOp, RemoveOp, MoveOp, ...>` plus a free function
  `apply(variant, doc)`. Faster (no v-table) and serializable for
  free, but every new mutator becomes a variant-edit + several
  switch-case edits. Inheritance + virtual is the right call for
  the "open-for-extension, closed-for-modification" axis we want
  here — adding a Command later means **one new file**.
  Re-evaluate when CRDT replication ships and we need a wire format
  (the variant has free serialization; the abstract Command needs
  visitor or tag dispatch). Defer that decision.
- **Per-keystroke commands without coalescing.** Typing 10
  characters → 10 entries in the undo stack → user undoes 10 times
  to get rid of one word. Production editors coalesce successive
  text edits into one undo step within a time / cursor / kind
  window. We don't implement coalescing yet; the architecture is
  ready for it (a `CoalescingCommand` wrapper or an `extend_top`
  on `UndoStack`). Follow-up.
- **Commands hold a Document pointer.** Loses CRDT / replay /
  cross-document portability. Rejected.
- **Two-phase commands (`apply`/`commit`/`rollback`).** Useful for
  composite multi-document transactions. Out of scope; the
  document is single-writer.

## Consequences

- New header: `domain/include/noted/domain/command/commands.hpp`
  (concrete commands), `.../command/undo_stack.hpp`.
- Existing `command.hpp` stub is replaced with the canonical
  abstract interface (`apply(Document&)` / `undo(Document&)` /
  `label()`); `command.cpp` carries all concrete impls + UndoStack.
- New public Document method: `restore_subtree(subtree, insert_index)`
  — the precise inverse of `remove_block`. Handles both the
  normal-parent case and the document-root case.
- 27 new unit tests in `tests/unit/command_test.cpp` covering:
  every command's apply/undo round-trip (incl. childless root
  removal), `restore_subtree` rejection paths, UndoStack
  execute/undo/redo state machine, redo invalidation on new edit,
  bounded depth, null-cmd guard, empty-stack errors, peek labels,
  realistic interleaved sequence.
- Total unit-test count: 136 → 163.
- No engine / compositor / shader changes.

## Follow-ups

- **P4 #12 `feat/file-format-mvp`** — serialize Document by either
  (a) writing the block tree directly, or (b) writing the
  command stream from project start and replaying on load. The
  command stream is forward-compatible (newer reader can replay
  older commands) but slower to load; the tree snapshot is
  compact + fast. Decide in the file-format ADR.
- **Edit coalescing** — `CoalescingCommand` wrapper that merges
  consecutive `SetPayloadCommand` writes within a typing window;
  or a `last_command.try_merge(new_command)` hook on UndoStack.
- **CRDT replication** — every command becomes a wire-format
  entry; commands grow a serialize/deserialize pair. The variant
  alternative becomes attractive again when we get there.
- **Macro commands** — a `CompositeCommand` that runs a sequence
  of children as one undo step (e.g. "paste 5 blocks").
- **Plugin API** — third-party code emits `Command` instances. The
  abstract base + label is already the right shape.
