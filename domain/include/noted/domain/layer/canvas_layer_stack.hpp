#pragma once

// CanvasLayerStack — the Goodnotes-side ordered ink-layer stack.
//
// A `CanvasLayerStack` is a small, ordered list of `CanvasLayer`
// entries. Each entry has a stable `LayerId` (allocated monotonically),
// a display name, a visibility flag, and an opacity. The list order is
// the **z-order**: index 0 is bottom, `size() - 1` is top. New strokes
// land on whichever layer the caller has marked "active"; the render
// path walks the list in order and consults `visible` to decide which
// strokes to draw.
//
// Why a separate type from `LayerGraph` (Photoshop-side comp DAG):
//   - Ink layers are an **ordered list**, not a DAG. Re-ordering ("move
//     this layer up") maps cleanly to index swaps; trying to express
//     it as input-list edits in a DAG is ceremony for no win.
//   - The Photoshop-side `LayerGraph` carries 16 blend modes + opacity
//     + adjustments + masks; the canvas-side ink stack initially needs
//     only visibility + name. The two will likely unify once the
//     `LayerGraph` powers raster image layers — this header is the
//     v1 of that future merger.
//
// Pure data + pure logic — no GPU, no I/O, no platform. All mutators
// return `Result<T>`; the stack never lands in a partially-mutated
// state on failure (validate-then-mutate, mirroring `LayerGraph` and
// `Document`'s contract).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "noted/domain/layer/layer.hpp"  // BlendMode
#include "noted/engine/canvas/layer_id.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain {

struct CanvasLayer {
    noted::LayerId id{noted::invalid_layer_id};
    std::string name;
    bool visible{true};
    // Locked layers reject paint input — the tool input gate refuses
    // to start a stroke when the active layer is locked, matching
    // Photoshop's "cannot paint on a locked layer" guard. Mirrors
    // the `visible` semantics: data persists across save/load so a
    // locked layer stays locked on the next launch.
    bool locked{false};
    // [0, 1] — multiplied onto every stroke's alpha at draw time.
    // The stroke engine's per-frame `opacity_for_layer` lookup
    // applies this multiplicatively to the ribbon alpha channel.
    float opacity{1.0F};
    // Per-layer blend mode. Wire-stable on disk (see ADR 0016 for
    // the `BlendMode` enum ordinal contract). v1 stores the value
    // but the stroke render path only honours `normal` end-to-end
    // — full per-layer blending across the 16 modes is the Phase C
    // GPU deliverable that drives `shaders/layer.slang`. Storing it
    // now means a v8 doc round-trips the user's intent through the
    // upgrade.
    noted::domain::BlendMode blend{noted::domain::BlendMode::normal};

    [[nodiscard]] auto operator==(const CanvasLayer&) const noexcept -> bool = default;
};

// Ordered list of canvas layers. The stack owns its own monotonic id
// allocator so layer IDs are stable for the lifetime of the document
// (no reuse on removal — references from in-flight strokes / undo
// snapshots never resolve to a different layer).
class CanvasLayerStack {
public:
    CanvasLayerStack() = default;
    CanvasLayerStack(const CanvasLayerStack&) = default;
    auto operator=(const CanvasLayerStack&) -> CanvasLayerStack& = default;
    CanvasLayerStack(CanvasLayerStack&&) noexcept = default;
    auto operator=(CanvasLayerStack&&) noexcept -> CanvasLayerStack& = default;
    ~CanvasLayerStack() = default;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return layers_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return layers_.empty(); }

    // Bottom-up traversal: index 0 is the bottom-most layer.
    [[nodiscard]] auto layers() const noexcept -> const std::vector<CanvasLayer>& {
        return layers_;
    }

    // Append a new layer at the **top** of the stack with the given
    // name. Returns the freshly-allocated id. Cannot fail; the Result
    // wrapper is kept for API symmetry with the rest of the mutation
    // surface.
    [[nodiscard]] auto add_layer(std::string name) -> Result<noted::LayerId>;

    // Insert a fully-formed layer at `index` (`index == size()` is an
    // append). The layer's `id` must be non-zero and not already
    // present; otherwise rejected. Used by the JSON loader and by
    // undo to restore a previously-removed layer at its original
    // position.
    [[nodiscard]] auto insert_layer(std::size_t index, CanvasLayer layer) -> Result<std::size_t>;

    // Remove the layer at `index`. Returns the removed layer (so undo
    // can re-insert it). Rejects with invalid_argument on out-of-range
    // index. Note: strokes still carrying the removed id keep it on
    // disk and become **invisible** at render time (the renderer skips
    // strokes whose layer_id no longer resolves). This avoids a
    // cascading "rewrite every stroke" pass on layer delete; the
    // caller decides whether to also issue a parallel `RemoveStroke`
    // batch.
    [[nodiscard]] auto remove_layer(std::size_t index) -> Result<CanvasLayer>;

    // Field mutators. Each rejects with invalid_argument on unknown id.
    // `opacity` is clamped to [0, 1]; NaN becomes 0.
    auto set_visible(noted::LayerId id, bool v) -> Result<void>;
    auto set_locked(noted::LayerId id, bool v) -> Result<void>;
    auto set_name(noted::LayerId id, std::string name) -> Result<void>;
    auto set_opacity(noted::LayerId id, float value) -> Result<void>;
    auto set_blend(noted::LayerId id, noted::domain::BlendMode mode) -> Result<void>;

    // Re-order: move the layer at `from` to `to`. `to` is the **target
    // index after removal** — `move(2, 0)` lifts the third layer to the
    // bottom. Rejects on out-of-range either side. Cheap O(n).
    auto move(std::size_t from, std::size_t to) -> Result<void>;

    // Find by id. Returns nullptr if unknown. O(n) but n is small.
    [[nodiscard]] auto find(noted::LayerId id) const noexcept -> const CanvasLayer*;

    // Stable check used by the renderer: is this id known AND visible?
    // Used as the cheap inner-loop predicate when filtering strokes
    // per frame. Unknown ids return `false` so strokes that survived
    // a layer delete stay hidden rather than blowing up.
    [[nodiscard]] auto is_visible(noted::LayerId id) const noexcept -> bool;

    // Loader bypass — used by the `.noted` JSON loader after it has
    // validated structure. Bumps `next_id_` past every restored id so
    // subsequent `add_layer` allocations stay monotonic. Regular
    // mutation must go through the mutators above (so undo / CRDT /
    // dirty-tracking see them).
    void replace(std::vector<CanvasLayer> layers) noexcept;

private:
    [[nodiscard]] auto index_of_(noted::LayerId id) const noexcept -> std::size_t;
    void rebuild_next_id_() noexcept;

    std::vector<CanvasLayer> layers_{};
    noted::LayerId next_id_{1};  // 0 is reserved for invalid_layer_id
};

}  // namespace noted::domain
