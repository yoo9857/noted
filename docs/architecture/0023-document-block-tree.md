# ADR 0023: Document — unified block tree

**Status:** Accepted
**Date:** 2026-05-18

## Context

The product unifies note-taking (Goodnotes-class) and raster image
editing (Photoshop-class) in one document. Until now the
domain-side stubs reflected this intent only as a forward-declared
`class Document` — the actual data structure was missing, blocking
every downstream feature (undo/redo, autosave, file format, search,
CRDT replication).

A senior decision point: should we model notes and images as **two
parallel data structures** (a `NotePage` tree and a `LayerGraph`)
or as **one tree** that contains both kinds of leaves? Every
cross-cutting concern below favors the unified tree:

| Concern | Two structures | One tree |
|---|---|---|
| Undo / redo | Per-domain command stack, awkward when an edit spans both | Single command stack over one model |
| File format | Two top-level schemas, version each independently | One schema |
| Search / index | Two indexers | One walker |
| CRDT replication | Two CRDTs, careful boundary | One CRDT |
| Cross-domain selection (e.g. "page 2 + this image") | Hand-rolled | Free |

The cost is that the tree node has to be polymorphic — a payload
variant — which we already do for `LayerGraph` payloads in the
compositor. Same pattern, applied one level up.

## Decision

`noted::domain::Document` is a strict **tree** of `BlockNode`s. The
tree is rooted at a single block (typically a `group`); every
non-root block has exactly one `parent` and appears in that parent's
ordered `children` list.

### Block kinds (wire-stable)

```cpp
enum class BlockKind : std::uint8_t {
    group   = 0,  // structural container — notebooks, pages, columns
    text    = 1,  // plain paragraph
    heading = 2,  // header with level 1..6
    code    = 3,  // monospace block with language hint
    canvas  = 4,  // ink canvas — points at a LayerGraph
    image   = 5,  // raster image + non-destructive edit graph
    embed   = 6,  // external link / URI / iframe
};
```

Numeric values are **on-disk stable**. New kinds append; nothing
reorders. The `.noted` file format (P4 #12) reads these by ordinal.

### Payload variant

```cpp
using BlockPayload = std::variant<
    GroupPayload, TextPayload, HeadingPayload, CodePayload,
    CanvasPayload, ImagePayload, EmbedPayload>;
```

Two helper functions pin the kind ↔ variant correspondence:
`default_payload_for(BlockKind)` and `kind_of(const BlockPayload&)`.
`set_payload()` rejects with `invalid_argument` if the supplied
payload's kind disagrees with the block's `kind`.

### Heavy data lives in side-stores

`CanvasPayload` and `ImagePayload` hold **opaque IDs** into external
stores — never the raw bytes:

| Payload | ID field | Resolved by |
|---|---|---|
| `CanvasPayload::graph_id` | `LayerGraphId` | LayerGraph store (one per ink canvas) |
| `ImagePayload::asset_id`  | `AssetId`      | Asset registry (PNG/JPG bytes) |
| `ImagePayload::edit_graph_id` | `LayerGraphId` | LayerGraph store (filter stack on the image) |

Why: a 4K image is 32 MB of payload. If the document inlined it,
every clone (for undo / diff / autosave) would copy 32 MB. With IDs,
the document is a few KB regardless of how many images it embeds.
Side-stores own the bytes and reference-count them.

### Tree representation: both parent and children pointers

Each `BlockNode` stores both `parent` (one BlockId) and `children`
(ordered vector of BlockId). The redundancy buys:

- **O(1) upward traversal** for cycle checks on `move_to`, breadcrumb
  rendering, and undo's "where did this come from" question.
- **O(1) downward traversal** in render order — children are already
  in display order.

The mutators keep both halves in sync; `validate()` proves the
representation is internally consistent (every parent agrees with
its parent's children list, no orphans, no duplicates).

### Mutation contract (validate-then-mutate)

Mirrors `LayerGraph` (ADR 0016):

- Every mutator returns `Result<T>`; no exceptions.
- On failure, the document is unchanged. We never half-apply.
- `add_block(kind, parent)` returns `Result<BlockId>`.
  `parent == invalid_block_id` allocates the root if and only if
  the document has no root yet; otherwise rejects.
- `remove_block(id)` removes the whole subtree.
- `move_to(id, new_parent, index)` rejects cycles via `O(depth)`
  parent-chain walk.
- `set_payload(id, payload)` rejects on kind mismatch.

### Why `add_block(invalid_block_id)` for the root

Three alternatives considered:

| API | Pros | Cons |
|---|---|---|
| `add_root(kind, name) -> Result<BlockId>` separate method | Loud, hard to misuse | Two ways to create a block, doubles call sites |
| `add_block(kind, parent=0)` (chosen) | One method | Sentinel value semantics |
| Constructor takes the root | Type-level invariant | Forces every test to spell out the root |

The sentinel version is the smallest API. The contract is checked
at runtime: subsequent attempts with `parent == invalid_block_id`
return `invalid_state`, so the misuse surface is one error message
away from the truth.

### Clear vs. remove(root)

`clear()` wipes the document and resets the root to invalid; it's
the only way to discard a populated root in one call. `next_id_` is
**not** reset across clears — any external reference (undo stack,
CRDT history) that holds a pre-clear ID will not collide with a
post-clear ID. IDs are monotonic for the entire document lifetime.

## Alternatives considered

- **Flat block list (Notion-style).** Blocks reference parents via
  `parent` only; ordering by a per-row index field. Cheaper inserts,
  but every render walk is `O(N)` per group instead of `O(children)`,
  and `move_to` becomes a sort-key edit. The tree's `children`
  vector is what makes render-order traversal cheap and stable.
- **Two structures (NotePage tree + LayerGraph).** Doubles every
  cross-cutting concern (see table above). Rejected.
- **JSON-as-DOM (no typed payload).** Validation moves to runtime
  every load; type errors crash the user's UI instead of failing
  the test suite. Rejected.
- **Heading as a property on a TextPayload.** Headings serialize
  differently, render differently, are searched separately, and
  outline panels treat them as first-class. Distinct kind keeps
  every consumer simpler.
- **No `group` kind — use the tree shape alone.** Then "page" and
  "notebook" have no identity in the data. Search ("find page named
  'Meeting'") becomes a heuristic on `name`. With an explicit
  `group` kind, structural containers are unambiguous.
- **Heavy data inlined in payload.** A 4K image / a 10k-stroke ink
  canvas inflates every diff and clone. Side-stores with opaque
  IDs is the only viable option at this scale.

## Consequences

- New module file pair:
  `domain/include/noted/domain/document/document.hpp` (was a stub),
  `domain/src/document/document.cpp` (was empty). Stub renamed
  `BlockType` → `BlockKind` for naming consistency with `LayerKind`.
- 30 unit tests in `tests/unit/document_test.cpp` cover: payload
  round-trip, root allocation (and the "no two roots" rule),
  add / insert at index, subtree removal, move with cycle / descendant
  / index-OOB / root rejection, payload kind-match enforcement,
  heading level clamping, preorder traversal, clear with
  monotonic-id guarantee, validate.
- Total unit-test count: 95 → 125.
- No engine / compositor / shader changes.
- `domain/CMakeLists.txt` already listed `src/document/document.cpp`;
  no build-system edits required.

## Follow-ups

- **P4 #11 `feat/command-undo-redo`** — Command pattern + undo stack
  on top of the block tree. Every mutator gets a paired `*_command`
  type that serializes its inputs.
- **P4 #12 `feat/file-format-mvp`** — `.noted` archive serializes
  the Document via its kind-discriminated payload variant; assets
  go into `assets/`; LayerGraph stores into `graphs/`; history into
  `history.bin`.
- **CRDT layer** — `domain/crdt/` is currently a stub. When CRDT
  replication lands, it sits between user input and `Document`
  mutations, applying ops in causal order.
- **Side-store types** — `LayerGraphStore` and `AssetRegistry` get
  concrete homes once a consumer (a renderer or the file format)
  forces a decision on lifetime / ownership / refcount.
