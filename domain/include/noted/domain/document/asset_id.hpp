#pragma once

// AssetId — opaque handle into the document's `ImageAssetRegistry`.
//
// Lives in its own header (rather than `document.hpp`) so leaf headers
// like `tool/image_input.hpp` can carry an `AssetId` field without
// pulling the full `Document` translation unit — and so `Document`
// can include `image_input.hpp` without creating an include cycle.
//
// Wire format: integer, monotonically allocated by `ImageAssetRegistry`.
// 0 is reserved (`invalid_asset_id`) and means "no asset" — a placeholder
// image primitive with `asset_id == invalid_asset_id` is the v0.x default
// before the file picker (B.7.b.2) decodes real bitmap data.

#include <cstdint>

namespace noted::domain {

using AssetId = std::uint64_t;
inline constexpr AssetId invalid_asset_id = 0;

}  // namespace noted::domain
