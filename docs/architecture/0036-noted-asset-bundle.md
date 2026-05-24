# ADR 0036 — `.noted` asset bundle in the zip archive

**Status:** Accepted. Implementation lands in `feat/noted-asset-bundle`.
**Date:** 2026-05-24
**Builds on:** [ADR 0026 (`.noted` archive container)](0026-noted-archive-container.md),
[ADR 0025 (`.noted` JSON format)](0025-document-json-format.md).
**Closes phase:** B.7.b.3 of the unified-canvas plan (image-tool slice).

## Context

`B.7.b.1` (PR #98) introduced `ImageAssetRegistry` and stamped an
`AssetId` onto every `ImagePrimitive`. `B.7.b.2` (PR #100) wired
`App::run_image_picker` to call `platform::image_io::load_rgba8` so
the picker captures the file's intrinsic dimensions and allocates a
fresh `AssetId`.

What both slices left undone: the **pixel payload never reaches
disk**. `run_image_picker` discards the decoded RGBA buffer right
after the dimension read; the registry stores only id +
source_path + intrinsic w/h. On `save_noted_file → load_noted_file`
the registry round-trips with its metadata intact but every
asset's bytes resurface as nothing. A `.noted` file moved between
machines (the headline reason users want a single-file container at
all) shows a placeholder rectangle where the author placed an
image.

The plan in `HANDOFF.md` always identified this as a third slice
(`B.7.b.3`). The original outline kept the pixels on the engine
side via "GPU registry" terminology, but the engine-side path was
deferred until B.7.b.2b's GPU upload — meanwhile the save/load
contract was promised but unimplemented.

This ADR commits to the contract and specifies it end-to-end.

## Decision

### Payload location: domain, not engine

`ImageAsset` gains a `std::vector<std::byte> source_bytes` field
holding the **original encoded file bytes** (the PNG / JPG / …
stream as it sat on disk when the user picked it). The decoded
RGBA expansion stays out of domain — that remains the
GPU-companion's job under B.7.b.2b — but the encoded payload IS
domain data: it's what gets persisted, what survives a re-save
without re-encoding, and what a future B.7.b.2b's GPU upload
re-decodes from.

Rejected alternatives:

- **Bytes live in an engine/app companion registry** — would
  require save/load to take an extra "blob provider" parameter,
  splitting the persistence contract across two API surfaces.
  Every caller (tests, App, future autosave) would have to set up
  the companion. Domain ownership is simpler and matches how every
  other persisted field already lives.
- **Bytes live in an engine companion that observes the document
  via hooks** — same problem plus an async lifecycle to reason
  about during save. The hook system is for runtime events; save
  is a synchronous request.

### Storage format: encoded source, not decoded RGBA

We store the **original** file's bytes verbatim — `[0x89, 'P',
'N', 'G', …]` for a PNG, `[0xFF, 0xD8, 0xFF, …]` for a JPG. Not
the decoded RGBA expansion. This is a 5–20× space saving for
typical content (PNG / JPG already pack their pixels) and
preserves the source-of-truth (re-saving never loses fidelity to
re-encoding artefacts). The codec is inferred at decode time
from the byte signature; `source_path` keeps the original
filename for UX only.

### Container layout

```
<archive>/
  document.json           — required (ADR 0025)
  assets/<asset-id>       — encoded payload, one per registry entry
                            whose source_bytes is non-empty
```

The asset member name omits a file extension — the codec is
inferred from the byte signature at decode time. Using the
decimal `AssetId` as the leaf name keeps the mapping
human-inspectable (`unzip -l` lines up with `image_assets[*].id`
in the JSON) and avoids depending on `source_path`'s extension
hint (which a hand-edited document.json could lie about).

### JSON schema bump: v9 → v10

No new keys. The bump is **informational**: it signals "this
writer's output may carry `assets/<id>` members in the zip"
without changing the document.json wire shape. A v9 reader can
still parse a v10 document.json without complaint (it just
doesn't know to look in the zip for asset members, which is fine
because v9 didn't promise bundled bytes). A v10 reader loading a
v6..v9 file silently leaves every asset's `source_bytes` empty —
matching the legacy behaviour.

We do NOT add a per-asset `"bytes_bundled": true` flag in the
JSON. The zip itself is the source of truth for blob presence:

- Member exists → load it into `source_bytes`.
- Member absent → leave `source_bytes` empty.

This means the JSON layer stays codec-agnostic (no claim about
whether bytes exist), and a tampered archive that strips
`assets/*` members produces the same result as a legacy file: a
visible placeholder, not an error. Stricter validation belongs at
the GPU upload boundary where the user-facing "image won't show"
signal originates.

### Compression: NONE for asset blobs

`document.json` continues to be DEFLATE-compressed (`MZ_DEFAULT_
COMPRESSION`). Asset blobs are stored with `MZ_NO_COMPRESSION`:
PNG / JPG already pack their pixels with DEFLATE / DCT
respectively, so re-deflating in the outer zip burns CPU at save
time with ~0 size benefit. A future addition for genuinely
compressible asset formats (raw BMP, raw RGBA) can swap the
default on a per-asset basis.

### Per-asset size cap

`platform::io::kMaxAssetBytes = 64 MiB`. An archive whose asset
member uncompresses past that limit fails the load. Sized for
multi-megapixel JPG photos with headroom; below the
`document.json`'s 256 MiB cap so the relative magnitudes stay
sensible.

A malicious `.noted` could still try to bundle 1000 assets at
64 MiB each (64 GiB total). A total-bundle cap is a reasonable
follow-up but isn't in this slice — the per-asset cap already
bounds the worst-case extract for a single zip member, and the
incremental-extraction loop walks the assets one at a time so a
process running out of memory shuts down before doing the next
extract.

## Implementation surface

**Domain (`domain/`)**:
- `ImageAsset::source_bytes` field (`std::vector<std::byte>`).
- `ImageAssetRegistry::attach_source_bytes(id, bytes)` mutator
  for the loader's second pass (looks up the asset by id, sets
  its `source_bytes`, returns `bool` for found-or-not).
- Default-generated equality includes the new field.

**Persistence (`domain/io/document_json.cpp`)**:
- `kDocumentJsonVersion = 10`. Header schema comment grows a v10
  paragraph clarifying the bump is zip-side only.
- No serializer or parser code change — the registry already
  round-trips `id / source_path / intrinsic_w_px / intrinsic_h_px`
  exactly as before.

**Archive (`platform/io/noted_file.cpp`)**:
- `kAssetEntryPrefix = "assets/"` + `kMaxAssetBytes = 64 MiB`
  in the header.
- `document_to_archive_bytes`: after writing `document.json`,
  iterate `doc.image_assets().assets()` and write each non-empty
  `source_bytes` as `assets/<id>` with `MZ_NO_COMPRESSION`.
- `document_from_archive_bytes`: after `document_from_json`
  returns, walk the registry's assets, probe the zip for
  `assets/<id>`, extract + `attach_source_bytes` when present.
  Cap-violations and stat/extract failures bubble up as
  `invalid_state` Errors.

**App (`app/src/app.cpp::run_image_picker`)**:
- Open the picked file once more and read its bytes into a
  `std::vector<std::byte>`. Assign to the new asset's
  `source_bytes` before `registry.allocate`.

**Tests**:
- `image_asset_registry_test.cpp` gains 4 cases for
  `attach_source_bytes` (missing id, live id, overwrite, empty
  bytes).
- `noted_file_test.cpp` gains 4 archive-asset cases (bundled
  bytes round-trip, empty payload produces no zip member,
  multiple assets independent, legacy registry-without-bytes
  loads cleanly).
- `document_json_test.cpp` updates the version-emit assertion
  from `"version": 9` to `"version": 10` and renames the
  "future-version rejected" fixture from `Version10` to
  `Version11`.

## Migration

- **v6..v9 files loaded under v10**: parse cleanly; each
  asset's `source_bytes` stays empty. Identical visual
  behaviour to loading under the original reader (placeholder
  rectangles).
- **v10 files loaded under v6..v9 readers**: parse cleanly; the
  v6..v9 readers never look for `assets/*` zip members so the
  payload is silently ignored. The user sees placeholders. This
  is a lossy direction — but it's the *only* lossy direction we
  introduce, and the loss is reversible (re-pick the image and
  save again under v10).
- **v10 → v10 round-trip**: bit-for-bit fidelity for the asset
  payload. The PNG/JPG bytes saved by the author come back out
  on every reader machine.

## Out of scope (deliberate)

- **B.7.b.2b — GPU upload**. The decoded RGBA path that consumes
  `source_bytes` lives in a separate slice; this ADR only ensures
  the bytes survive disk.
- **Asset garbage collection**. v0.x keeps every registered asset
  pinned for the document lifetime; even an asset whose only
  referencing primitive was removed stays on disk on the next
  save. A reference-count + sweep pass lands when the lifecycle
  becomes user-visible (next to "Properties → Replace asset…"
  or similar).
- **Total-bundle size cap**. Per-asset cap is in. Total-bundle
  cap (sum over assets) is a reasonable follow-up but not
  required for the threat model in scope (single hostile asset
  at 64 MiB is recoverable; total budget is bounded by `.noted`
  file size).
- **Asset reference counting via `Document::images()` ↔
  `image_assets()`**. The JSON loader already enforces the one
  direction we care about (every `ImagePrimitive::asset_id`
  resolves in the registry); the reverse (every registry asset
  is referenced by some primitive) is not enforced.
- **External-storage assets**. v0.x assumes every asset is
  bundled; we don't yet support "link to an external file by
  URL or relative path." A future schema bump can add that as
  an alternative payload form (`source_url` + content hash).
