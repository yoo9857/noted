#pragma once

// Demo scene used by the v0.x scaffold app — a four-layer LayerGraph
// that exercises every fixed-function blend mode the compositor
// implements (normal / linear_dodge / multiply). Replacing this with
// a `Document` loaded from disk is a follow-up; for v0.x it proves
// the LayerCompositor pipeline is wired end-to-end.

#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/layer/layer.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::app {

struct DemoScene {
    noted::domain::LayerGraph graph;
    noted::compositor::LayerPayloadStore store;
};

// Stacked deterministically via explicit input edges so the topological
// walk lands the same order on every run:
//
//     bg (normal)        — dark navy base
//      └── red (normal)  — semi-transparent red over bg
//           └── add (linear_dodge)  — additive cool blue glow
//                └── warm (multiply) — warm-tone tint over everything
//
// Result on screen: dark navy → muted red → blue glow → warm overlay.
[[nodiscard]] auto build_demo_scene() -> noted::Result<DemoScene>;

}  // namespace noted::app
