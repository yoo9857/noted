#pragma once

// JSON serialization for `domain::Document` — the v1 on-disk format
// for the `.noted` file format.
//
// API surface is intentionally narrow: two free functions taking /
// returning `std::string`. The nlohmann/json type is an
// implementation detail and does not leak through this header, so the
// rest of the codebase doesn't transitively pick up its 25k-line
// single header.
//
// Schema summary (see ADR 0025 for the full spec):
//
//   {
//     "version": 2,
//     "root": <BlockId>,                 // 0 when document is empty
//     "blocks": [
//       {
//         "id":       <BlockId>,
//         "kind":     <integer ordinal>, // BlockKind value, wire-stable per ADR 0023
//         "visible":  <bool>,
//         "name":     <string>,
//         "parent":   <BlockId>,         // 0 for the root
//         "children": [<BlockId>, ...],
//         "payload":  { ...kind-specific... }
//       },
//       ...
//     ],
//     "pages": {                         // optional in v1; required in v2 (may be empty)
//       "gap_px": <float>,
//       "items": [
//         { "w": <float>, "h": <float>, "bg": <int>, "x": <float> },
//         ...
//       ]
//     }
//   }
//
// Versions:
//   - v1: blocks only. Loader accepts these for back-compat.
//   - v2: adds `pages`. Writer always emits v2. v1 files load with an
//     empty page list.
//
// The payload object's keys depend on the block's kind. See ADR 0025
// for the table. Group blocks emit `{}`.
//
// Parser is strict: unknown top-level keys, unknown payload keys, and
// type mismatches are all rejected. This catches typos early and
// prevents silent data loss when a future schema removes a field that
// older readers may have written.
//
// Rationale: see docs/architecture/0025-document-json-format.md.

#include <string>
#include <string_view>

#include "noted/engine/error/error.hpp"

namespace noted::domain {
class Document;
}  // namespace noted::domain

namespace noted::domain::io {

// On-disk schema version. Writer always emits this; reader accepts
// this value AND every prior supported version (currently 1).
inline constexpr int kDocumentJsonVersion = 2;
inline constexpr int kDocumentJsonMinReadableVersion = 1;

// Serialize `doc` to JSON. Output is pretty-printed with 2-space
// indent for diff-friendliness; that's worth ~30% extra bytes at the
// document scale we expect (a few KB).
[[nodiscard]] auto document_to_json(const Document& doc) -> std::string;

// Parse JSON into a Document. Errors on:
//   - malformed JSON (`invalid_argument` with parser context).
//   - missing or unsupported `version` (`invalid_state`).
//   - structurally invalid block list (duplicate id, dangling parent,
//     parent/children mismatch, kind/payload mismatch) —
//     `invalid_state`.
//   - unknown top-level keys or unknown payload fields — strict
//     mode catches typos before they silently round-trip.
//
// On any failure the returned Result holds the error; no
// partially-built Document is returned.
[[nodiscard]] auto document_from_json(std::string_view json) -> Result<Document>;

}  // namespace noted::domain::io
