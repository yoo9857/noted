#pragma once

// Layer graph — the Photoshop side's data backbone.
//
// A LayerGraph is a directed acyclic graph of LayerNodes. Each node has a
// kind (bitmap, stroke, adjustment, group, mask), a blend mode, an
// opacity, a visibility flag, an optional name, and a list of input layer
// IDs. The graph also has a designated `root` node — the layer whose
// composited output is the document's final image.
//
// Non-destructive editing falls out of the graph:
//   - An "adjustment layer" with the photo as its sole input applies the
//     adjustment without touching the photo's pixels.
//   - A "group" reroutes through its children; collapsing a group
//     re-evaluates dependents because the topology changes.
//   - Reordering = swapping `inputs` lists.
//
// This module is **pure data**. No GPU, no I/O, no platform. The
// compositor (P3 #8) walks the graph and asks the GPU to render each
// node's output into the canvas; payload data (image handles, stroke
// collections) is referenced by opaque IDs that the compositor resolves.
//
// Rationale: see docs/architecture/0016-layer-domain-model.md.

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::domain {

using LayerId = std::uint64_t;
inline constexpr LayerId invalid_layer_id = 0;

// Standard Photoshop set. Numeric values are stable on disk; new modes
// append, never reorder.
enum class BlendMode : std::uint8_t {
    normal       =  0,
    multiply     =  1,
    screen       =  2,
    overlay      =  3,
    soft_light   =  4,
    hard_light   =  5,
    color_dodge  =  6,
    color_burn   =  7,
    linear_dodge =  8,  // a.k.a. add
    linear_burn  =  9,
    difference   = 10,
    exclusion    = 11,
    hue          = 12,
    saturation   = 13,
    color        = 14,
    luminosity   = 15,
};

// Stable on disk. Payload structs for each kind live in their own headers
// once we wire concrete data (P3 #8 onward).
enum class LayerKind : std::uint8_t {
    bitmap     = 0,  // raster pixels (RGBA texture)
    stroke     = 1,  // vector ink stroke collection
    adjustment = 2,  // pure pixel transform with one input (curves, levels…)
    group      = 3,  // folder; passes its child's output through
    mask       = 4,  // 1-channel alpha modulator applied to another layer
};

struct LayerNode {
    LayerId               id      {invalid_layer_id};
    LayerKind             kind    {LayerKind::bitmap};
    BlendMode             blend   {BlendMode::normal};
    float                 opacity {1.0F};  // [0, 1]
    bool                  visible {true};
    std::string           name;
    std::vector<LayerId>  inputs;  // upstream nodes whose output feeds this one
};

// DAG of LayerNodes. All mutators return Result<T> — the graph never
// throws and never lands in a partially-mutated state on failure.
//
// Identity scheme: LayerIds are monotonically allocated and never reused
// within the lifetime of a graph. invalid_layer_id (0) is reserved.
//
// Removal policy: remove_layer rejects when other nodes still reference
// the layer as an input. The caller decides whether to rewire those
// inputs first or accept the failure — the graph never silently drops
// references.
class LayerGraph {
public:
    LayerGraph() = default;
    LayerGraph(LayerGraph&&) noexcept = default;
    auto operator=(LayerGraph&&) noexcept -> LayerGraph& = default;
    LayerGraph(const LayerGraph&) = default;
    auto operator=(const LayerGraph&) -> LayerGraph& = default;
    ~LayerGraph() = default;

    // Allocate a layer and return its fresh ID. After this the caller may
    // call set_inputs / set_blend / set_opacity / etc. to fill it in.
    [[nodiscard]] auto add_layer(LayerKind kind, std::string name = {}) -> LayerId;

    // Remove a layer. Rejects with invalid_state if any other node lists
    // `id` in its inputs, or with invalid_argument if `id` is unknown.
    auto remove_layer(LayerId id) -> Result<void>;

    // Replace the layer's input list. Rejects (without mutating) on:
    //   - invalid_argument: id or any input is unknown / invalid.
    //   - invalid_state:    inputs contain `id` itself or would close a cycle.
    auto set_inputs(LayerId id, std::span<const LayerId> inputs) -> Result<void>;

    // Field mutators. Each returns invalid_argument if id is unknown.
    // opacity is clamped to [0, 1]; NaN becomes 0.
    auto set_blend  (LayerId id, BlendMode mode)      -> Result<void>;
    auto set_opacity(LayerId id, float value)         -> Result<void>;
    auto set_visible(LayerId id, bool v)              -> Result<void>;
    auto set_name   (LayerId id, std::string name)    -> Result<void>;

    // The root node is the one whose composited output is the document
    // image. set_root rejects if id is unknown. By convention,
    // invalid_layer_id means "no root yet"; the compositor treats that
    // as an empty document.
    auto set_root(LayerId id) -> Result<void>;
    [[nodiscard]] auto root() const noexcept -> LayerId { return root_; }

    // Lookup. Returns nullptr if id is unknown.
    [[nodiscard]] auto find(LayerId id) const noexcept -> const LayerNode*;

    // Snapshot of all nodes. Order is unspecified — use
    // topological_order() if you need dependency-respecting iteration.
    [[nodiscard]] auto nodes() const -> std::vector<const LayerNode*>;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return nodes_.size(); }

    // Topological order: every node appears after all its inputs. Errors
    // with invalid_state on cycle or dangling reference. Cost O(V + E).
    [[nodiscard]] auto topological_order() const -> Result<std::vector<LayerId>>;

    // Whole-graph well-formedness check: every input id is known, no
    // cycles, root (if set) is in the graph. Cheap O(V + E) — the
    // compositor calls this before any frame submit.
    [[nodiscard]] auto validate() const -> Result<void>;

private:
    std::unordered_map<LayerId, LayerNode> nodes_;
    LayerId                                root_    {invalid_layer_id};
    LayerId                                next_id_ {1};  // 0 is reserved
};

}  // namespace noted::domain
