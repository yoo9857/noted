#include "noted/domain/layer/layer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace noted::domain {

namespace {

// DFS colors for cycle detection.
enum class Mark : std::uint8_t { white, gray, black };

// Walks the DAG starting at `start` and returns true if any node already
// `gray` is encountered (= back-edge = cycle). Uses an explicit stack so
// deep graphs don't blow the C++ stack.
auto has_cycle_from(
    const std::unordered_map<LayerId, LayerNode>& nodes,
    LayerId                                       start,
    std::unordered_map<LayerId, Mark>&            marks) -> bool {
    struct Frame {
        LayerId      id;
        std::size_t  next_input;  // index into nodes[id].inputs
    };
    std::vector<Frame> stack;
    stack.push_back({start, 0});
    marks[start] = Mark::gray;

    while (!stack.empty()) {
        auto& top = stack.back();
        const auto it = nodes.find(top.id);
        if (it == nodes.end()) {
            // Dangling reference — treated as "no cycle here" so validate()
            // can flag it separately with a better error message.
            marks[top.id] = Mark::black;
            stack.pop_back();
            continue;
        }
        const auto& node = it->second;
        if (top.next_input >= node.inputs.size()) {
            marks[top.id] = Mark::black;
            stack.pop_back();
            continue;
        }
        const LayerId child = node.inputs[top.next_input++];
        const auto m_it = marks.find(child);
        const Mark m = (m_it == marks.end()) ? Mark::white : m_it->second;
        if (m == Mark::gray) {
            return true;  // back-edge
        }
        if (m == Mark::white) {
            marks[child] = Mark::gray;
            stack.push_back({child, 0});
        }
        // black: already fully explored, skip.
    }
    return false;
}

[[nodiscard]] auto sanitize_opacity(float v) noexcept -> float {
    if (std::isnan(v)) {
        return 0.0F;
    }
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

}  // namespace

auto LayerGraph::add_layer(LayerKind kind, std::string name) -> LayerId {
    const auto id = next_id_++;
    LayerNode n{};
    n.id   = id;
    n.kind = kind;
    n.name = std::move(name);
    nodes_.emplace(id, std::move(n));
    return id;
}

auto LayerGraph::remove_layer(LayerId id) -> Result<void> {
    if (id == invalid_layer_id || nodes_.find(id) == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::remove_layer: unknown id " + std::to_string(id)));
    }
    // Reject if anyone still points at it.
    for (const auto& [_, node] : nodes_) {
        if (std::find(node.inputs.begin(), node.inputs.end(), id) !=
            node.inputs.end()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "LayerGraph::remove_layer: " + std::to_string(id) +
                " still referenced as input by " + std::to_string(node.id)));
        }
    }
    nodes_.erase(id);
    if (root_ == id) {
        root_ = invalid_layer_id;
    }
    return {};
}

auto LayerGraph::set_inputs(LayerId id, std::span<const LayerId> inputs)
    -> Result<void> {
    auto it = nodes_.find(id);
    if (id == invalid_layer_id || it == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_inputs: unknown id " + std::to_string(id)));
    }
    // Validate every input first so we never mutate on partial failure.
    for (const auto in : inputs) {
        if (in == invalid_layer_id || nodes_.find(in) == nodes_.end()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                "LayerGraph::set_inputs: input " + std::to_string(in) +
                " is unknown / invalid"));
        }
        if (in == id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "LayerGraph::set_inputs: self-reference on " + std::to_string(id)));
        }
    }

    // Tentatively swap in the new inputs, run a cycle check, and roll
    // back if it fails. Cheaper than building a copy-of-graph for the
    // check (and matches the "no partial mutation on failure" contract).
    auto& node = it->second;
    std::vector<LayerId> previous = node.inputs;
    node.inputs.assign(inputs.begin(), inputs.end());

    std::unordered_map<LayerId, Mark> marks;
    if (has_cycle_from(nodes_, id, marks)) {
        node.inputs = std::move(previous);
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "LayerGraph::set_inputs: would create a cycle through " +
            std::to_string(id)));
    }
    return {};
}

auto LayerGraph::set_blend(LayerId id, BlendMode mode) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_blend: unknown id " + std::to_string(id)));
    }
    it->second.blend = mode;
    return {};
}

auto LayerGraph::set_opacity(LayerId id, float value) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_opacity: unknown id " + std::to_string(id)));
    }
    it->second.opacity = sanitize_opacity(value);
    return {};
}

auto LayerGraph::set_visible(LayerId id, bool v) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_visible: unknown id " + std::to_string(id)));
    }
    it->second.visible = v;
    return {};
}

auto LayerGraph::set_name(LayerId id, std::string name) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_name: unknown id " + std::to_string(id)));
    }
    it->second.name = std::move(name);
    return {};
}

auto LayerGraph::set_root(LayerId id) -> Result<void> {
    if (id == invalid_layer_id) {
        root_ = invalid_layer_id;
        return {};
    }
    if (nodes_.find(id) == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerGraph::set_root: unknown id " + std::to_string(id)));
    }
    root_ = id;
    return {};
}

auto LayerGraph::find(LayerId id) const noexcept -> const LayerNode* {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

auto LayerGraph::nodes() const -> std::vector<const LayerNode*> {
    std::vector<const LayerNode*> out;
    out.reserve(nodes_.size());
    for (const auto& [_, n] : nodes_) {
        out.push_back(&n);
    }
    return out;
}

auto LayerGraph::topological_order() const -> Result<std::vector<LayerId>> {
    if (auto v = validate(); !v) {
        return std::unexpected(std::move(v).error());
    }
    // Iterative DFS post-order yields inputs-before-dependents. Stable
    // iteration order across the unordered_map is not required — the
    // compositor will sort by depth or visit-order; topological order
    // is a partial order, multiple valid linearizations exist.
    std::unordered_map<LayerId, Mark> marks;
    std::vector<LayerId> out;
    out.reserve(nodes_.size());

    struct Frame {
        LayerId      id;
        std::size_t  next_input;
    };
    std::vector<Frame> stack;

    for (const auto& [start_id, _] : nodes_) {
        if (marks[start_id] != Mark::white) {
            continue;
        }
        stack.push_back({start_id, 0});
        marks[start_id] = Mark::gray;
        while (!stack.empty()) {
            auto& top = stack.back();
            const auto& node = nodes_.at(top.id);
            if (top.next_input >= node.inputs.size()) {
                marks[top.id] = Mark::black;
                out.push_back(top.id);
                stack.pop_back();
                continue;
            }
            const LayerId child = node.inputs[top.next_input++];
            const Mark m = marks[child];
            if (m == Mark::gray) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "LayerGraph::topological_order: cycle at " +
                    std::to_string(child)));
            }
            if (m == Mark::white) {
                marks[child] = Mark::gray;
                stack.push_back({child, 0});
            }
        }
    }
    return out;
}

auto LayerGraph::validate() const -> Result<void> {
    // Dangling references.
    for (const auto& [_, node] : nodes_) {
        for (const auto in : node.inputs) {
            if (in == invalid_layer_id || nodes_.find(in) == nodes_.end()) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "LayerGraph::validate: node " + std::to_string(node.id) +
                    " references unknown input " + std::to_string(in)));
            }
        }
    }
    // Root presence.
    if (root_ != invalid_layer_id && nodes_.find(root_) == nodes_.end()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "LayerGraph::validate: root " + std::to_string(root_) +
            " is not in the graph"));
    }
    // Cycles — DFS every connected component.
    std::unordered_map<LayerId, Mark> marks;
    for (const auto& [id, _] : nodes_) {
        if (marks[id] != Mark::white) {
            continue;
        }
        if (has_cycle_from(nodes_, id, marks)) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "LayerGraph::validate: cycle reachable from " +
                std::to_string(id)));
        }
    }
    return {};
}

}  // namespace noted::domain
