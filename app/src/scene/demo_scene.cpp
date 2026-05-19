#include "scene/demo_scene.hpp"

#include <array>
#include <utility>

namespace noted::app {

auto build_demo_scene() -> noted::Result<DemoScene> {
    using BM = noted::domain::BlendMode;
    using LK = noted::domain::LayerKind;
    DemoScene s;

    // The graph is still wired with the four demo layers + their
    // blend modes + input edges so the layer panel has something
    // to show and toggle. **Payloads are intentionally omitted**
    // so the compositor walks the graph and emits no draws — the
    // page background from PageRenderer is what the user sees
    // instead. Re-add the SolidColor payloads to put the demo
    // canvas-fill back as a regression check, or wire real
    // bitmap payloads (P3 follow-up) for actual image layers.
    const auto bg = s.graph.add_layer(LK::bitmap, "background");
    const auto red = s.graph.add_layer(LK::bitmap, "red");
    const auto add = s.graph.add_layer(LK::bitmap, "additive glow");
    const auto warm = s.graph.add_layer(LK::bitmap, "warm tint");

    if (auto r = s.graph.set_blend(red, BM::normal); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_opacity(red, 0.60F); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_blend(add, BM::linear_dodge); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_opacity(add, 0.50F); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_blend(warm, BM::multiply); !r) {
        return std::unexpected(std::move(r).error());
    }

    // Chain the topology so the topological walk produces bg → red → add → warm.
    if (auto r = s.graph.set_inputs(red, std::array{bg}); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_inputs(add, std::array{red}); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_inputs(warm, std::array{add}); !r) {
        return std::unexpected(std::move(r).error());
    }

    // Payload registration deliberately skipped — see the
    // top-of-function comment.

    return s;
}

}  // namespace noted::app
