#pragma once

// Per-layer payload — the actual pixel data each layer contributes.
//
// `domain::LayerGraph` carries topology + parameters (blend mode, opacity,
// inputs). The compositor needs more than that to render: each node needs
// its concrete contribution — bitmap pixels, a stroke collection,
// adjustment parameters, etc. We deliberately keep those payloads OUT of
// `LayerGraph` so the domain stays GPU-free and serialization-clean.
//
// This module is the bridge: a per-LayerId payload store the compositor
// queries while walking the topological order. Domain operations (insert,
// move, rename) don't touch the store; payload mutations (upload pixels,
// add stroke) don't touch the graph. They join only at composite-time.
//
// MVP scope: the only payload kind shipped today is `SolidColor` — a
// single pre-multiplied-alpha RGBA. Enough to demonstrate the compositor
// architecture and blend-mode plumbing without dragging in bitmap upload
// or per-layer stroke buffers. Future kinds (Bitmap, Stroke, Adjustment)
// add to the variant; the compositor's main switch grows by one branch
// per kind.
//
// Rationale: see docs/architecture/0019-layer-compositor.md.

#include <cstdint>
#include <unordered_map>
#include <variant>

#include "noted/domain/layer/layer.hpp"

namespace noted::compositor {

// MVP payload: a flat color. Premultiplied alpha so the compositor's
// blend math is well-defined (the SRC_ALPHA factor is built into the
// .r/.g/.b values).
struct SolidColor {
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};
};

// Variant over all supported kinds. New kinds add a struct and an entry;
// the compositor's switch grows by one branch.
using LayerPayload = std::variant<SolidColor>;

// LayerId → payload mapping. Trivial map; the compositor reads, the
// host (app / brush UI / etc.) writes.
class LayerPayloadStore {
public:
    void set(noted::domain::LayerId id, LayerPayload p) { store_[id] = std::move(p); }

    // nullptr if no payload registered for this id. Compositor treats
    // that as "skip this layer" — a no-op pass-through to its inputs.
    [[nodiscard]] auto find(noted::domain::LayerId id) const noexcept -> const LayerPayload* {
        auto it = store_.find(id);
        return it == store_.end() ? nullptr : &it->second;
    }

    void erase(noted::domain::LayerId id) { store_.erase(id); }
    void clear() noexcept { store_.clear(); }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return store_.size(); }

private:
    std::unordered_map<noted::domain::LayerId, LayerPayload> store_;
};

}  // namespace noted::compositor
