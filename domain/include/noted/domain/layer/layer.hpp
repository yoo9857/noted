#pragma once

#include <cstdint>
#include <vector>

namespace noted::domain {

using LayerId = std::uint64_t;
using NodeId  = std::uint64_t;

// Non-destructive editing: every layer is a DAG of operation nodes.
// The output of each node is cached on the GPU and only invalidated
// when an input changes.
enum class BlendMode : std::uint8_t {
    normal, multiply, screen, overlay, soft_light, hard_light,
    color_dodge, color_burn, linear_dodge, linear_burn, difference,
    exclusion, hue, saturation, color, luminosity,
};

struct LayerNode {
    NodeId id{};
    BlendMode blend{BlendMode::normal};
    float opacity{1.0f};
    bool  visible{true};
    std::vector<NodeId> inputs;
};

class LayerGraph;

}  // namespace noted::domain
