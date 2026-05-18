# ADR 0025: Document JSON serialization (v1 of the `.noted` format)

**Status:** Accepted
**Date:** 2026-05-18

## Context

`domain::Document` (ADR 0023) is in place, with mutators
(`add_block`, `move_to`, `restore_subtree`) and the Command +
UndoStack (ADR 0024) wired on top. The next prerequisite for any
end-to-end product flow is **persistence**: users save work, close
the app, reopen, and pick up where they left off.

The full `.noted` archive needs more than just the document tree —
it will eventually hold:

1. `document.json` — the block tree (this ADR).
2. `assets/<id>.<ext>` — referenced image / pen-stroke bytes.
3. `graphs/<id>.json` — LayerGraph instances referenced from
   CanvasPayload / ImagePayload.
4. `history.bin` — optional Command stream for replayable history.

Stuffing all four into one PR creates a wide review surface and
two new dependencies (a JSON lib + a zip lib). To keep the change
boundary tight and the work auditable, this PR ships **only #1**:
the JSON schema + round-trip for the block tree. The container
layer and assets/graphs/history slot in via follow-up ADRs.

## Decision

### Format: JSON

`document.json` is a UTF-8 JSON object. Trade-offs considered:

| Format | Size | Speed | Debuggability | Tooling | Notes |
|---|---|---|---|---|---|
| **JSON** (chosen) | medium | medium | excellent | universal | Human-readable, diff-friendly, version-tooling-friendly |
| MessagePack / CBOR | small | fast | poor (binary) | growing | Worth revisiting if document sizes ever push past tens of MB |
| FlatBuffers | smallest | fastest | poor | niche | Schema-coupled; updates require schema regeneration |
| Custom binary | smallest | fastest | poor | none | Reinvents the wheel |

JSON wins for the v1 product:
- Block-tree size is dominated by **structure**, not content
  (heavy bytes live in `assets/`). Even a 10k-block tree is < 1 MB
  of JSON.
- Users + reviewers + git diffs all benefit from readable output.
- nlohmann/json is single-header, MIT, ubiquitous, and used at
  every level of the C++ ecosystem.
- Migration to binary later is a single ADR if profiling demands
  it. The `version` field gates the choice.

### Schema (v1)

```jsonc
{
  "version": 1,
  "root": <BlockId>,                 // 0 when document is empty
  "blocks": [
    {
      "id":       <BlockId>,         // > 0
      "kind":     <integer ordinal>, // matches BlockKind enum value
      "visible":  <bool>,
      "name":     <string>,
      "parent":   <BlockId>,         // 0 for root
      "children": [<BlockId>, ...],
      "payload":  { ...kind-specific... }
    },
    ...
  ]
}
```

**Block array order is pre-order** (root first, parents before
their children). The writer emits via `Document::preorder()`; the
reader relies on `restore_subtree`'s pre-order validation to catch
files violating this invariant.

### Payload tables (per `kind`)

| Kind ordinal | Name | Payload object keys |
|---:|---|---|
| 0 | `group`   | `{}` (empty) |
| 1 | `text`    | `content` (string) |
| 2 | `heading` | `content` (string), `level` (int, clamped to 1..6 on load) |
| 3 | `code`    | `content` (string), `language` (string) |
| 4 | `canvas`  | `graph_id` (uint64), `width` (uint32), `height` (uint32) |
| 5 | `image`   | `asset_id` (uint64), `edit_graph_id` (uint64) |
| 6 | `embed`   | `uri` (string) |

The payload object is **strict**: unknown keys are rejected.
Catches typos before they round-trip silently.

### Why integer kind ordinals (and not enum names)

ADR 0023 made `BlockKind`'s integer values wire-stable: "new kinds
append, never reorder; the `.noted` file format depends on the
integer values." We honor that here. Trade-offs vs. string names:

- Renaming `text` → `paragraph` in code costs **zero**
  schema-version bumps with integers.
- Older readers parsing a newer file see the same ordinal value
  whether the writer renamed the C++ enumerator or not.
- Cost: less human-readable. Mitigated by the (future) on-disk
  layout reserving `kind` as a comment-friendly integer with a
  legend in this ADR + writer tooling.

### Version policy

- `version` is a single integer, monotonically increasing.
- Reader **rejects** files with an unknown future version
  (cannot guarantee a safe interpretation).
- Reader **accepts** files at the current version (today: 1).
- Older versions get a migration step in the reader when a
  schema-incompatible change ships — no such step exists yet.

### Strict parser

The reader rejects:
- Malformed JSON (with parser context in the error message).
- Missing `version` / unsupported `version`.
- Unknown top-level keys (`{"version": 1, "root": 0, "blocks": [],
  "stowaway": 1}` → error).
- Unknown per-block keys.
- Unknown payload keys (per kind).
- `id == 0` for any block.
- `kind` ordinal outside the enum range.
- Empty `blocks` with non-zero `root` (or vice versa).
- `blocks[0]` whose id doesn't match the top-level `root`.
- `blocks[0]` whose `parent` isn't `0`.
- A child block whose `parent` isn't an earlier entry in `blocks`
  (catches non-pre-order files).
- Structural inconsistencies that `Document::restore_subtree`
  already catches (duplicate id, dangling child reference).

Strict mode is the right call for v1: typos surface immediately,
the schema is the source of truth, and no callers exist yet that
need a more permissive mode. A loose mode (e.g., warn + drop
unknown keys) can land as a parser flag if/when forward
compatibility with un-versioned changes becomes a real need.

### `next_id` is recovered, not stored

The writer does NOT emit `Document::next_id_`. The reader
reconstructs it via `restore_subtree`, which advances
`next_id_` past every restored id. This avoids a subtle failure
mode where someone hand-edits a JSON file, bumps the highest id
above `next_id`, saves, reloads, and gets id collisions on
subsequent allocations. By making `next_id` recoverable from the
file contents, the JSON has one less source of truth to keep in
sync.

### API surface — narrow on purpose

```cpp
namespace noted::domain::io {
inline constexpr int kDocumentJsonVersion = 1;

auto document_to_json(const Document&) -> std::string;
auto document_from_json(std::string_view) -> Result<Document>;
}
```

The `nlohmann::json` type does NOT appear in the public header.
The library is a single 25k-line include; pulling it into every
TU that touches `Document` would balloon parse times across the
codebase. The implementation pays the cost once, in
`document_json.cpp`.

Consumers serialize / parse through `std::string`. When the zip
container ships, it wraps these strings into a `document.json`
archive entry.

## Alternatives considered

- **MessagePack / CBOR** — Same shape as JSON in code; binary on
  disk. ~30% smaller and ~3x faster to parse. Premature: the block
  tree is small even on big documents. Revisit if profiling shows
  load time matters. Decision-gate: 10 MB+ documents reaching
  > 100 ms load time.
- **FlatBuffers** — Zero-copy and fast, but the schema lives in a
  `.fbs` file and the generated headers add a build step. Worth
  it for shared memory / wire transfer. Overkill for a file
  format.
- **Custom binary** — Smallest, fastest, opaque. Lower-effort
  alternatives win at this scale.
- **Store `next_id` explicitly** — Adds a source-of-truth duplicate
  that hand-edits can corrupt silently. Rejected.
- **String enum names instead of integer ordinals** — More
  human-readable. Trade-off didn't survive: ADR 0023's "new kinds
  append" rule favors integers, and the rename-without-bumping-
  version property is more valuable than naming.
- **Loose parser (warn on unknown fields)** — Pushes the typo
  burden to runtime. Forward compatibility is theoretical today;
  add a flag if/when a real need arises.
- **YAML instead of JSON** — More writable, less ubiquitous in
  C++. Trade-off didn't survive: file format I/O isn't a place
  we want a less-common parser.

## Consequences

- New dependency: nlohmann/json v3.11.3 via FetchContent, SYSTEM
  include. ~25k-line header in one TU (`document_json.cpp`).
- New module file pair:
  `domain/include/noted/domain/io/document_json.hpp`,
  `domain/src/io/document_json.cpp`.
- `domain/CMakeLists.txt` gains `nlohmann_json::nlohmann_json` as
  a PRIVATE_DEPS — the JSON lib is implementation-only.
- 26 new unit tests in `tests/unit/document_json_test.cpp` cover
  round-trip (empty, single root, all 7 kinds, deep tree, id +
  order preservation, visibility), the full rejection matrix
  (16 cases), heading level clamping on load, monotonic next_id
  after load, schema sanity for the emitted text.
- Total unit-test count: 163 → 189.
- No engine / compositor / shader changes.

## Follow-ups

- **Zip container** — wrap `document.json` (and future
  `assets/` / `graphs/`) in a single `.noted` file. miniz
  single-header, public domain. One follow-up PR, isolated
  surface.
- **Asset registry serialization** — assets/<id>.<ext> entries
  with a manifest. Lands when the first asset-consuming feature
  ships (image import).
- **LayerGraph serialization** — graphs/<id>.json. Lands when
  Canvas / Image blocks have real consumers.
- **Command history** — history.bin replay stream. Trade-off
  between full-tree snapshot vs. command-stream-only-on-load
  documented in ADR 0024's follow-ups.
- **MessagePack alternative** — Revisit when load-time profiling
  has actual numbers.
- **Format-fuzz harness** — feed random byte streams into
  `document_from_json` to verify the parser never crashes /
  consumes unbounded memory on adversarial input.
