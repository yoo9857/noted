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
//     "version": 3,
//     "root": <BlockId>,                 // 0 when document is empty
//     "blocks": [ ... ],                 // same as prior versions
//     "pages": { ... },                  // same as v2
//     "shapes": [                        // v3+; optional (loader treats absent as empty)
//       {
//         "k":   <int>,                  // ShapeKind ordinal (rectangle=0, ellipse=1)
//         "x0":  <float>, "y0": <float>,
//         "x1":  <float>, "y1": <float>,
//         "sw":  <float>,                // stroke_width_px
//         "r":   <float>, "g": <float>, "b": <float>, "a": <float>
//       },
//       ...
//     ]
//   }
//
// Versions:
//   - v1: blocks only. Loader accepts these for back-compat.
//   - v2: adds `pages`. v1 files load with an empty page list.
//   - v3: adds `shapes`. v1/v2 files load with an empty shape list.
//   Writer always emits the current version.
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
// this value AND every prior supported version.
inline constexpr int kDocumentJsonVersion = 3;
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
