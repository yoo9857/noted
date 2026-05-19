#include "scene/demo_scene.hpp"

#include <array>
#include <utility>

namespace noted::app {

auto build_demo_scene() -> noted::Result<DemoScene> {
    using BM = noted::domain::BlendMode;
    using LK = noted::domain::LayerKind;
    DemoScene s;

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

    s.store.set(bg, noted::compositor::SolidColor{0.15F, 0.18F, 0.22F, 1.0F});
    s.store.set(red, noted::compositor::SolidColor{0.85F, 0.25F, 0.30F, 1.0F});
    s.store.set(add, noted::compositor::SolidColor{0.20F, 0.45F, 0.95F, 1.0F});
    s.store.set(warm, noted::compositor::SolidColor{0.95F, 0.85F, 0.70F, 1.0F});

    return s;
}

}  // namespace noted::app
