# ADR 0026: `.noted` archive — zip container for the file format

**Status:** Accepted
**Date:** 2026-05-18

## Context

ADR 0025 shipped the JSON layer of the `.noted` file format —
`domain::io::document_to_json` / `document_from_json` round-trip
the block tree as a string. The format's stated container shape
(see ADR 0025's "Follow-ups") is an archive that bundles the
document JSON with future assets and LayerGraph stores into one
portable artifact.

This ADR pins the container choice and ships the wrapping layer.
It deliberately splits from #35: introducing a new dependency
(miniz) gets one focused PR with its own CI matrix, separate from
the JSON layer's nlohmann/json introduction.

## Decision

`.noted` is a **standard ZIP archive** built / read via
[**miniz**](https://github.com/richgel999/miniz) 3.0.2 (single-
header, public domain).

### Container layout (v1)

```
<archive>/
  document.json    — required, the block tree (ADR 0025)
  assets/          — reserved; v1 readers ignore
  graphs/          — reserved; v1 readers ignore
```

The v1 reader requires `document.json` and ignores anything else.
That gives the format **forward compatibility**: a v2 writer can
add `assets/<id>.png` and `graphs/<id>.json` without bumping a
schema version; v1 readers silently drop the unknown entries but
still recover the document tree. Old `.noted` files keep opening
in newer apps; new `.noted` files keep opening (with reduced
fidelity) in older apps.

### Why ZIP

| Option | Pros | Cons |
|---|---|---|
| **ZIP (chosen)** | Universal: every OS unzips it. Per-entry compression. Forward-compatible layout (`assets/`, `graphs/`, future subdirs). Single-header lib. | Slight overhead vs. raw bytes for tiny documents. |
| TAR | Streamable; simpler structure. | No random access; users on Windows need 7-zip or PowerShell. Same dep cost as miniz. |
| Custom binary | Smallest; fastest. | Reinvents wheels; users can't inspect; recoverability is on us. |
| Plain directory | Zero parsing cost; native filesystem inspection. | Not a single-file artifact; loses portability over email / drive / git LFS. |

The product wants users to **email a `.noted` file** or **drag it
into a folder**. That's a single-artifact requirement — rules out
plain directories. ZIP wins on universality + tooling + the
forward-compatible namespacing convention.

### Why miniz (not libarchive / Boost.Zip / cppzip / minizip-ng)

- **miniz** is one ~9k-line C file, public domain, zero
  configuration, builds clean across MSVC / GCC / Clang. The
  zip subset it supports is everything we need (read + write
  STORE / DEFLATE, no encryption, no zip64-only paths yet).
- **libarchive** is the right answer for "support every archive
  format ever", but it adds a hard dep on bzip2/lzma/zstd
  libraries and ~50k+ lines of build. Overkill.
- **Boost.Zip** doesn't exist as a standalone, and pulling in
  Boost for one feature is not a trade we make.
- **minizip-ng** is good but has more deps (zlib, optional
  bzip2/lzma) and a larger surface. Revisit if/when we need
  zip64 / AES-256 / etc.

### API: bytes / filesystem split

```cpp
namespace noted::platform::io {

inline constexpr const char* kDocumentEntryName = "document.json";

auto document_to_archive_bytes(const Document&) -> std::vector<std::byte>;
auto document_from_archive_bytes(std::span<const std::byte>) -> Result<Document>;

auto save_noted_file(const std::filesystem::path&, const Document&) -> Result<void>;
auto load_noted_file(const std::filesystem::path&) -> Result<Document>;

}
```

**bytes layer** is pure logic — no filesystem side effects.
Used by:
- Unit tests (no temp-file plumbing).
- In-memory autosave to a ring buffer.
- Future cloud-sync paths (push the bytes directly to a server).
- Future undo of "open a different file" — keep the previous
  bytes in RAM as a checkpoint.

**filesystem layer** wraps the bytes layer with `std::ofstream` /
`std::ifstream`. Path encoding works correctly across platforms
because std::fstream's `std::filesystem::path` constructor uses
the native wide API on Windows (C++17+). No manual `_wfopen`
detour needed.

### Why a placeholder constant + reader-by-name

The reader locates `document.json` by name (`mz_zip_reader_locate_file`)
rather than by zip index. This isolates the loader from any
re-ordering miniz might do during the write side — additions in
`assets/` / `graphs/` will append in arbitrary order, and we
don't want the document loader to depend on that order.

The `kDocumentEntryName` constant lives in the public header so
tests can probe for it as a tripwire — anyone renaming the entry
will break every existing `.noted` file, and the
`DocumentEntryNameIsStable` unit test fires on the rename.

### Error contract

`document_to_archive_bytes` returns `std::vector<std::byte>`
(not `Result<...>`). On miniz allocation / state-init failure
it returns an empty vector. Rationale: serialization can't fail
on a well-formed Document; the bytes layer is intentionally
unfussy so the test suite can call it inline. The caller's
filesystem wrapper (`save_noted_file`) re-checks for an empty
result and surfaces the error.

`document_from_archive_bytes` is `Result<Document>` — every
reasonable failure mode (empty input, non-zip bytes, truncated
zip, missing entry, JSON parse failure) reports through
`Result::error()` with a useful message.

### 256 MB extraction cap

The reader rejects any `document.json` entry whose uncompressed
size exceeds 256 MB. Rationale: a malicious / corrupt archive
could declare a 100 GB uncompressed size and DOS the loader via
heap exhaustion. The cap is wildly generous for a JSON block tree
(real documents are kilobytes); when we add `assets/` the cap
applies per-entry, and a 4K PNG fits comfortably.

## Alternatives considered

- **Bundle JSON layer + zip layer in one PR.** Would have meant
  two new third-party deps + two new layers + ~50 tests in one
  review. Splitting was the right call — each PR has a focused
  CI matrix and clean rollback path.
- **Store `document.json` uncompressed (STORE mode).** Slightly
  faster I/O. Loses the ~3x compression ratio JSON gets from
  DEFLATE for free. Trade-off didn't survive: even a 100 KB JSON
  saves to a 30 KB archive; on cheap storage that matters for
  cloud sync.
- **`document.bin` (MessagePack) inside the zip.** Premature
  optimization; the JSON tier is small even at scale.
  Re-evaluate when block trees push past 1 MB.
- **No reserved subdirs in v1.** Then the v2 layout
  (`assets/<id>` etc.) requires a schema version bump. The cost
  of reserving the subdirs in this ADR is one paragraph; the cost
  of bumping the schema is migration code on every reader. Reserve
  now.
- **Encryption / signing.** Out of scope. When it lands it'll
  ride on top of zip (most likely a libsodium box around the
  whole archive) rather than miniz's deprecated `_with_password_`
  paths.

## Consequences

- New dep: miniz 3.0.2 via FetchContent SYSTEM. Provides
  `miniz::miniz` (alias `miniz`) target. ~9k-line C; one TU.
- One CMake compat shim: `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)`
  before `FetchContent_MakeAvailable(miniz)` because miniz's
  `cmake_minimum_required(VERSION 2.8.12)` is below CMake 4's
  hard floor. Confined to miniz's scope and unset afterward.
- New module file pair:
  `platform/include/noted/platform/io/noted_file.hpp`,
  `platform/src/io/noted_file.cpp`.
- `platform/CMakeLists.txt`: adds the file, makes `noted::domain`
  a PUBLIC_DEPS (the header transitively exposes `Document` as a
  parameter type), miniz as PRIVATE_DEPS.
- 13 new unit tests in `tests/unit/noted_file_test.cpp` cover:
  empty + rich document round-trip (in-memory + filesystem),
  zip-magic sanity, overwrite-on-save, every rejection path
  (empty buffer, non-zip bytes, truncated zip, nonexistent file,
  non-zip file, empty file), empty-document file round-trip,
  entry-name stability tripwire.
- Total unit-test count: 189 → 202.
- HANDOFF "no file format" entry retired.

## Follow-ups

- **Asset registry serialization** — write image bytes into
  `assets/<asset_id>.<ext>` on save; load lazily on demand.
  Lands when image-import ships.
- **LayerGraph serialization** — `graphs/<graph_id>.json` entries
  for canvas and image edit stacks. Lands when LayerGraph has
  concrete consumers.
- **Command-history stream** — `history.bin` (or
  `history.jsonl`). Trade-off (snapshot vs. command-stream-on-
  load) deferred to its own ADR; the file format reserves the
  filename.
- **Atomic save** — `save_noted_file` currently overwrites in
  place. Production-grade flow: write to `<path>.tmp`, fsync,
  rename. Add when first user reports a power-loss data-loss bug,
  or proactively before public release.
- **Streaming load** — every entry is currently extracted to
  heap. For huge embedded assets, switch to an iterator API that
  yields entries to the caller without materializing all bytes at
  once.
- **Zip64** — miniz supports zip64 reads but the writer defaults
  to legacy format. If a single document or asset exceeds 4 GB
  this matters; for v1 the 256 MB cap keeps us well under.
- **Format fuzz harness** — feed random byte streams into
  `document_from_archive_bytes` to verify the parser never crashes
  or consumes unbounded memory on adversarial input. Stub belongs
  next to the existing `tests/fuzz/` scaffold.
