#pragma once

// ImageAssetRegistry — side-store for raster image assets referenced by
// the document's `ImagePrimitive` table.
//
// Phase B.7.b.1. Establishes the persistence + identity contract so
// B.7.b.2 (file picker + stb_image decode + GPU upload) can drop in
// without churning the schema, and B.7.b.3 (zip-bundled asset blobs)
// can key binary entries by `AssetId`.
//
// What lives here (domain layer — pure data):
//   - `ImageAsset` POD: id + source_path + intrinsic dimensions +
//     `source_bytes` (the original encoded file bytes — PNG / JPG /
//     etc — as picked by the user; B.7.b.3). Intentionally NO
//     **decoded** RGBA pixels: those live in a GPU-side companion
//     registry (B.7.b.2b). Encoded bytes are kept in domain because
//     they ARE the persisted payload — the `.noted` zip stores them
//     verbatim — and because re-decoding from them is the load
//     path's source of truth.
//
// What does NOT live here:
//   - Decoded RGBA buffers, Vulkan textures, `ImTextureID`s. Those
//     belong on the engine/app side and are looked up per-frame via
//     `AssetId`.
//   - Refcount / GC. v0.x keeps assets pinned for the document
//     lifetime; freeing assets when the last referencing primitive
//     is removed lands when the file picker exists to demonstrate
//     the lifecycle in practice.
//
// Allocation:
//   - `allocate(spec)`: assigns the next monotonic `AssetId` (1, 2, …),
//     overwrites `spec.id`, stores, returns the id. Use on the
//     interactive path (B.7.b.2 file picker).
//   - `insert(asset)`: stores with the caller's explicit id. Used by
//     the JSON loader to reconstruct the registry. Rejects:
//       * `id == invalid_asset_id`
//       * duplicate id
//     On success advances `next_id_` past the inserted value, so
//     subsequent `allocate` calls stay monotonic.
//
// `next_id_` is intentionally NOT reset by `clear()` — IDs stay
// monotonic across clears so any history / undo references survive
// the wipe (mirrors `Document::clear`'s contract).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "noted/domain/document/asset_id.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain {

// Persisted metadata + encoded payload for one image asset. The
// `source_bytes` member holds the original encoded file (PNG, JPG, …)
// and is what gets serialized into the `.noted` zip under
// `assets/<id>` keyed by this asset's `id`. Intrinsic dimensions are
// 0 when the asset hasn't been decoded yet.
struct ImageAsset {
    AssetId id{invalid_asset_id};

    // Original filesystem path the asset was loaded from, if any.
    // Used for UX (showing the source in the Properties panel) and as
    // a recovery hint when the bundled bytes are missing. Empty when
    // the asset originated from a clipboard paste or other in-memory
    // source. Not relied on at load time — `source_bytes` is the
    // authoritative payload.
    std::string source_path;

    // Intrinsic decoded dimensions in pixels. 0 means "not yet
    // decoded" — valid in v0.x because B.7.b.1 ships before B.7.b.2's
    // decode path. Once a decode succeeds these are pinned for the
    // life of the asset.
    std::uint32_t intrinsic_w_px{0};
    std::uint32_t intrinsic_h_px{0};

    // Original encoded file bytes (PNG / JPG / …). Populated by the
    // file picker (`App::run_image_picker`) and round-tripped through
    // the `.noted` zip's `assets/<id>` member. Empty for v6..v9 files
    // loaded under a v10 reader (bytes were never bundled), for
    // placeholder primitives (asset_id == invalid_asset_id), and for
    // freshly-allocated `ImageAsset{}` instances. The GPU upload path
    // (B.7.b.2b) re-decodes from these bytes — keeping them around
    // also means a user can re-save a loaded `.noted` without losing
    // assets the original author bundled.
    std::vector<std::byte> source_bytes;

    [[nodiscard]] auto operator==(const ImageAsset&) const noexcept -> bool = default;
};

class ImageAssetRegistry {
public:
    ImageAssetRegistry() = default;

    // Allocate a fresh `AssetId`, store the asset with that id, and
    // return it. `spec.id` is overwritten — callers do not need (and
    // should not try) to set it themselves.
    [[nodiscard]] auto allocate(ImageAsset spec) -> AssetId;

    // Insert with caller-supplied id. **Loader path only.** Rejects
    // `id == invalid_asset_id` or a duplicate of an already-stored
    // id with `invalid_argument`. On success, `next_id_` advances to
    // `max(next_id_, id + 1)` so subsequent `allocate` calls remain
    // monotonic.
    auto insert(ImageAsset asset) -> Result<void>;

    // Remove an asset by id. Returns true if removed, false if the
    // id was not present. Does NOT cascade into primitives that
    // reference the asset — the caller is responsible for keeping
    // the document consistent. (B.7.b.2 wires this through commands.)
    auto remove(AssetId id) -> bool;

    // Attach previously-bundled encoded bytes to an existing asset.
    // Used by the `.noted` loader (B.7.b.3) after extracting an
    // `assets/<id>` zip member: the JSON parser builds the registry
    // with `source_bytes` empty, then the platform layer fills each
    // asset's bytes in a second pass. Returns true on success; false
    // if no asset with `id` exists (a corrupt archive that names a
    // blob for a non-existent asset; caller decides whether that's
    // fatal or just a stranded member to skip).
    auto attach_source_bytes(AssetId id, std::vector<std::byte> bytes) -> bool;

    // Look up by id. Returns nullptr if absent.
    [[nodiscard]] auto find(AssetId id) const noexcept -> const ImageAsset*;

    // Stable iteration order = insertion / allocation order. Used by
    // the JSON serializer to keep file diffs deterministic.
    [[nodiscard]] auto assets() const noexcept -> const std::vector<ImageAsset>& { return assets_; }

    [[nodiscard]] auto empty() const noexcept -> bool { return assets_.empty(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return assets_.size(); }

    // Drop every asset. `next_id_` stays monotonic across clears so
    // any history / undo references survive the wipe.
    void clear() noexcept;

    [[nodiscard]] auto operator==(const ImageAssetRegistry&) const noexcept -> bool = default;

private:
    std::vector<ImageAsset> assets_;
    AssetId next_id_{1};  // 0 is reserved
};

}  // namespace noted::domain
