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
//     "version": 8,
//     "root": <BlockId>,                 // 0 when document is empty
//     "blocks": [ ... ],                 // same as prior versions
//     "pages": { ... },                  // same as v2
//     "shapes": [ ... ],                 // same as v3
//     "texts":  [ ... ],                 // same as v4
//     "images": [                        // v5+; v6 adds the "aid" field
//       {
//         "x":   <float>, "y": <float>,
//         "w":   <float>, "h": <float>,  // dimensions in canvas px (>= 1)
//         "r":   <float>, "g": <float>, "b": <float>, "a": <float>,
//         "aid": <uint64>                // v6+; AssetId into image_assets, 0 = placeholder
//       },
//       ...
//     ],
//     "image_assets": [                  // v6+; optional (loader treats absent as empty)
//       {
//         "id":  <uint64>,               // monotonically allocated, != 0
//         "src": <string>,               // original filesystem path (may be empty)
//         "iw":  <uint32>, "ih": <uint32> // intrinsic decoded dimensions (0 if not decoded)
//       },
//       ...
//     ],
//     "strokes": [                       // v7+; optional (loader treats absent as empty)
//       {
//         "mode":    <int>,              // DrawMode ordinal (draw=0, erase=1)
//         "lid":     <uint64>,           // v8+; LayerId, 0 = unassigned
//         "samples": [x0, y0, p0, x1, y1, p1, ...],  // flat float array (x, y, pressure triples)
//         "style": {
//           "min_r": <float>, "max_r": <float>,
//           "soft":  <float>, "ag":    <float>,
//           "r":     <float>, "g":     <float>,
//           "b":     <float>, "a":     <float>,
//           "stab":  <float>             // input-stabilizer weight
//         }
//       },
//       ...
//     ],
//     "canvas_layers": {                 // v8+; optional (absent ⇒ legacy migration)
//       "active": <uint64>,              // currently-active LayerId, 0 = none
//       "items": [                       // bottom-up z-order
//         {
//           "id":      <uint64>,         // monotonically allocated, != 0
//           "name":    <string>,
//           "visible": <bool>,
//           "opacity": <float>           // [0, 1]
//         },
//         ...
//       ]
//     }
//   }
//
// Versions:
//   - v1: blocks only. Loader accepts these for back-compat.
//   - v2: adds `pages`. v1 files load with an empty page list.
//   - v3: adds `shapes`. v1/v2 files load with an empty shape list.
//   - v4: adds `texts`. v1/v2/v3 files load with an empty text list.
//   - v5: adds `images`. v1..v4 files load with an empty image list.
//   - v6: adds `aid` on images + `image_assets` table. v5 files load
//         with every image's asset_id = invalid_asset_id (= 0) and an
//         empty `image_assets` registry. Strict referential integrity
//         on v6+: every non-zero `aid` must resolve in `image_assets`.
//   - v7: adds `strokes`. v1..v6 files load with an empty stroke list.
//         Sample arrays use a flat float layout (x, y, pressure
//         triples) for compactness — a 500-sample stroke is ~12 KB
//         smaller than the equivalent array-of-objects.
//   - v8: adds `canvas_layers` + per-stroke `lid` (LayerId). v7 files
//         with any strokes are migrated on load: a default "Layer 1"
//         is created and every imported stroke is pinned to its id,
//         so v7 round-trip semantics stay intact under v8 readers.
//         Strokes whose `lid` doesn't resolve in `canvas_layers` are
//         loaded as-is (renderer treats them as hidden — see
//         `CanvasLayerStack::is_visible` contract).
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
inline constexpr int kDocumentJsonVersion = 8;
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
