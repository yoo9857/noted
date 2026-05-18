#pragma once

// `.noted` archive — the v1 single-file container that wraps the
// document tree + (future) assets + LayerGraph stores into one
// portable artifact users can email, sync, or check into git.
//
// Container layout (v1):
//
//     <archive>/
//       document.json          — required, the block tree (ADR 0025)
//       assets/                — reserved; v1 readers ignore
//       graphs/                — reserved; v1 readers ignore
//
// Format: standard ZIP via miniz. Choosing zip over a custom binary:
//   - Users can unzip with any OS tool to inspect / recover.
//   - The structure is forward-compatible: `assets/<id>.<ext>` and
//     `graphs/<id>.json` slot in without bumping a schema version.
//   - Library cost is one single-header dep (miniz, public domain).
//
// API split:
//   - bytes-level: `document_to_archive_bytes` /
//     `document_from_archive_bytes`. Pure logic on `std::vector<std::byte>`.
//     Used by tests and any callers that want to avoid filesystem
//     side effects (e.g. autosave to a memory ring, in-process IPC).
//   - filesystem-level: `save_noted_file` / `load_noted_file` wrap
//     the bytes layer with file open/close. Path handling is
//     UTF-8 across platforms; on Windows the path is round-tripped
//     through wchar to honor non-ASCII filenames.
//
// Rationale: see docs/architecture/0026-noted-archive-container.md.

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::domain {
class Document;
}  // namespace noted::domain

namespace noted::platform::io {

// Filename of the document entry inside the archive. Constant so
// tests can assert on it and the loader can probe.
inline constexpr const char* kDocumentEntryName = "document.json";

// Serialize `doc` into a complete `.noted` archive in memory. Never
// fails on a well-formed Document; the byte vector is the caller's
// to keep / write / network-send / hash.
[[nodiscard]] auto document_to_archive_bytes(const noted::domain::Document& doc)
    -> std::vector<std::byte>;

// Parse a `.noted` archive from `bytes`. Errors on:
//   - bytes is empty or not a valid zip.
//   - archive does not contain `document.json`.
//   - `document.json` payload fails JSON parsing or schema validation
//     (errors flow through from `document_from_json`).
[[nodiscard]] auto document_from_archive_bytes(std::span<const std::byte> bytes)
    -> Result<noted::domain::Document>;

// Write `doc` as a `.noted` archive at `path`. Overwrites if the file
// exists. Creates parent directories as needed via the bytes layer
// + a single fwrite; concurrent writers to the same path race the
// usual filesystem rules.
//
// Cross-platform path handling: on Windows the path is opened via
// `_wfopen` after a wstring conversion so non-ASCII filenames work
// regardless of the system code page. On POSIX the path's
// `string()` is passed straight through.
[[nodiscard]] auto save_noted_file(const std::filesystem::path& path,
                                   const noted::domain::Document& doc) -> Result<void>;

// Read a `.noted` archive at `path`. Errors on file-not-found,
// read-permission failure, and every condition listed for
// `document_from_archive_bytes`.
[[nodiscard]] auto load_noted_file(const std::filesystem::path& path)
    -> Result<noted::domain::Document>;

}  // namespace noted::platform::io
