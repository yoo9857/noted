#pragma once

// LayerId — a stable opaque identifier for a canvas layer.
//
// Defined at the engine-canvas level rather than inside the domain so
// that `noted::stroke::Stroke` (engine module) can carry one without
// pulling in `noted/domain/layer/layer.hpp`. The same numeric id space
// is shared by:
//
//   - `Stroke::layer_id`  (engine — per-primitive routing field)
//   - `domain::LayerGraph::LayerId` (domain — Photoshop-side comp DAG)
//   - `domain::CanvasLayerStack::LayerId` (domain — Goodnotes-side ink
//      layer stack carried by `Document`)
//
// `0` is reserved as the "unassigned" / "unknown" sentinel. Documents
// allocate ids monotonically per stack so a numeric id is unique within
// its owning container (but not globally across two independent
// documents). Persisted on disk as a plain integer — see ADR 0025 for
// the `.noted` JSON contract; the value never reorders.

#include <cstdint>

namespace noted {

using LayerId = std::uint64_t;

inline constexpr LayerId invalid_layer_id = 0;

}  // namespace noted
