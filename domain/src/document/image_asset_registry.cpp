#include "noted/domain/document/image_asset_registry.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace noted::domain {

auto ImageAssetRegistry::allocate(ImageAsset spec) -> AssetId {
    const AssetId id = next_id_++;
    spec.id = id;
    assets_.push_back(std::move(spec));
    return id;
}

auto ImageAssetRegistry::insert(ImageAsset asset) -> Result<void> {
    if (asset.id == invalid_asset_id) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "ImageAssetRegistry::insert: asset.id == invalid_asset_id"));
    }
    for (const auto& a : assets_) {
        if (a.id == asset.id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                "ImageAssetRegistry::insert: duplicate asset id " + std::to_string(asset.id)));
        }
    }
    if (asset.id >= next_id_) {
        next_id_ = asset.id + 1;
    }
    assets_.push_back(std::move(asset));
    return {};
}

auto ImageAssetRegistry::remove(AssetId id) -> bool {
    const auto it = std::find_if(
        assets_.begin(), assets_.end(), [id](const ImageAsset& a) { return a.id == id; });
    if (it == assets_.end()) {
        return false;
    }
    assets_.erase(it);
    return true;
}

auto ImageAssetRegistry::attach_source_bytes(AssetId id, std::vector<std::byte> bytes) -> bool {
    const auto it = std::find_if(
        assets_.begin(), assets_.end(), [id](const ImageAsset& a) { return a.id == id; });
    if (it == assets_.end()) {
        return false;
    }
    it->source_bytes = std::move(bytes);
    return true;
}

auto ImageAssetRegistry::find(AssetId id) const noexcept -> const ImageAsset* {
    for (const auto& a : assets_) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

void ImageAssetRegistry::clear() noexcept {
    assets_.clear();
    // next_id_ intentionally preserved — see header.
}

}  // namespace noted::domain
