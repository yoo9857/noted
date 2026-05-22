#include "noted/domain/layer/canvas_layer_stack.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace noted::domain {

namespace {

[[nodiscard]] auto clamp_opacity(float value) noexcept -> float {
    if (std::isnan(value)) {
        return 0.0F;
    }
    if (value < 0.0F) {
        return 0.0F;
    }
    if (value > 1.0F) {
        return 1.0F;
    }
    return value;
}

}  // namespace

auto CanvasLayerStack::add_layer(std::string name) -> Result<noted::LayerId> {
    CanvasLayer layer{};
    layer.id = next_id_++;
    layer.name = std::move(name);
    layer.visible = true;
    layer.opacity = 1.0F;
    layers_.push_back(std::move(layer));
    return layers_.back().id;
}

auto CanvasLayerStack::insert_layer(std::size_t index, CanvasLayer layer) -> Result<std::size_t> {
    if (layer.id == noted::invalid_layer_id) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "CanvasLayerStack::insert_layer: layer.id must be non-zero"));
    }
    if (index > layers_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "CanvasLayerStack::insert_layer: index out of range"));
    }
    // Reject duplicate id. O(n) scan — n is small.
    for (const auto& existing : layers_) {
        if (existing.id == layer.id) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "CanvasLayerStack::insert_layer: id already present"));
        }
    }
    layer.opacity = clamp_opacity(layer.opacity);
    // Bump the id allocator past restored ids so future add_layer calls
    // never collide with a value already in the stack.
    if (layer.id >= next_id_) {
        next_id_ = layer.id + 1;
    }
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
    return index;
}

auto CanvasLayerStack::remove_layer(std::size_t index) -> Result<CanvasLayer> {
    if (index >= layers_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "CanvasLayerStack::remove_layer: index out of range"));
    }
    auto removed = std::move(layers_[index]);
    layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

auto CanvasLayerStack::set_visible(noted::LayerId id, bool v) -> Result<void> {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::set_visible: id not found"));
    }
    layers_[idx].visible = v;
    return {};
}

auto CanvasLayerStack::set_locked(noted::LayerId id, bool v) -> Result<void> {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::set_locked: id not found"));
    }
    layers_[idx].locked = v;
    return {};
}

auto CanvasLayerStack::set_blend(noted::LayerId id, noted::domain::BlendMode mode) -> Result<void> {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::set_blend: id not found"));
    }
    layers_[idx].blend = mode;
    return {};
}

auto CanvasLayerStack::set_name(noted::LayerId id, std::string name) -> Result<void> {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::set_name: id not found"));
    }
    layers_[idx].name = std::move(name);
    return {};
}

auto CanvasLayerStack::set_opacity(noted::LayerId id, float value) -> Result<void> {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::set_opacity: id not found"));
    }
    layers_[idx].opacity = clamp_opacity(value);
    return {};
}

auto CanvasLayerStack::move(std::size_t from, std::size_t to) -> Result<void> {
    if (from >= layers_.size() || to >= layers_.size()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "CanvasLayerStack::move: index out of range"));
    }
    if (from == to) {
        return {};
    }
    auto entry = std::move(layers_[from]);
    layers_.erase(layers_.begin() + static_cast<std::ptrdiff_t>(from));
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(to), std::move(entry));
    return {};
}

auto CanvasLayerStack::find(noted::LayerId id) const noexcept -> const CanvasLayer* {
    const auto idx = index_of_(id);
    if (idx == layers_.size()) {
        return nullptr;
    }
    return &layers_[idx];
}

auto CanvasLayerStack::is_visible(noted::LayerId id) const noexcept -> bool {
    const auto* layer = find(id);
    return (layer != nullptr) && layer->visible;
}

void CanvasLayerStack::replace(std::vector<CanvasLayer> layers) noexcept {
    layers_ = std::move(layers);
    for (auto& l : layers_) {
        l.opacity = clamp_opacity(l.opacity);
    }
    rebuild_next_id_();
}

auto CanvasLayerStack::index_of_(noted::LayerId id) const noexcept -> std::size_t {
    if (id == noted::invalid_layer_id) {
        return layers_.size();
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].id == id) {
            return i;
        }
    }
    return layers_.size();
}

void CanvasLayerStack::rebuild_next_id_() noexcept {
    noted::LayerId max_id = 0;
    for (const auto& l : layers_) {
        if (l.id > max_id) {
            max_id = l.id;
        }
    }
    next_id_ = max_id + 1;
}

}  // namespace noted::domain
