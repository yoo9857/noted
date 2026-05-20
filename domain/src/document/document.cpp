#include "noted/domain/document/document.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace noted::domain {

namespace {

[[nodiscard]] auto clamp_heading_level(std::uint8_t v) noexcept -> std::uint8_t {
    if (v < 1U) {
        return 1U;
    }
    if (v > 6U) {
        return 6U;
    }
    return v;
}

}  // namespace

auto default_payload_for(BlockKind kind) -> BlockPayload {
    switch (kind) {
        case BlockKind::group:
            return GroupPayload{};
        case BlockKind::text:
            return TextPayload{};
        case BlockKind::heading:
            return HeadingPayload{};
        case BlockKind::code:
            return CodePayload{};
        case BlockKind::canvas:
            return CanvasPayload{};
        case BlockKind::image:
            return ImagePayload{};
        case BlockKind::embed:
            return EmbedPayload{};
    }
    return TextPayload{};  // unreachable for well-formed BlockKind values
}

auto kind_of(const BlockPayload& payload) noexcept -> BlockKind {
    return std::visit(
        [](const auto& p) noexcept -> BlockKind {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, GroupPayload>) {
                return BlockKind::group;
            } else if constexpr (std::is_same_v<T, TextPayload>) {
                return BlockKind::text;
            } else if constexpr (std::is_same_v<T, HeadingPayload>) {
                return BlockKind::heading;
            } else if constexpr (std::is_same_v<T, CodePayload>) {
                return BlockKind::code;
            } else if constexpr (std::is_same_v<T, CanvasPayload>) {
                return BlockKind::canvas;
            } else if constexpr (std::is_same_v<T, ImagePayload>) {
                return BlockKind::image;
            } else {
                static_assert(std::is_same_v<T, EmbedPayload>,
                              "unhandled BlockPayload alternative");
                return BlockKind::embed;
            }
        },
        payload);
}

auto Document::add_block(BlockKind kind, BlockId parent, std::string name) -> Result<BlockId> {
    // Root-allocation special case: only valid when the document is empty.
    if (parent == invalid_block_id) {
        if (root_ != invalid_block_id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::add_block: document already has a root; pass a real parent"));
        }
        const auto id = next_id_++;
        BlockNode node{};
        node.id = id;
        node.kind = kind;
        node.name = std::move(name);
        node.parent = invalid_block_id;
        node.payload = default_payload_for(kind);
        nodes_.emplace(id, std::move(node));
        root_ = id;
        return id;
    }
    auto parent_it = nodes_.find(parent);
    if (parent_it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::add_block: unknown parent " + std::to_string(parent)));
    }
    const auto id = next_id_++;
    BlockNode node{};
    node.id = id;
    node.kind = kind;
    node.name = std::move(name);
    node.parent = parent;
    node.payload = default_payload_for(kind);
    nodes_.emplace(id, std::move(node));
    parent_it->second.children.push_back(id);
    return id;
}

auto Document::insert_block(BlockKind kind,
                            BlockId parent,
                            std::size_t index,
                            std::string name) -> Result<BlockId> {
    if (parent == invalid_block_id) {
        // Root insertion via insert_block isn't supported — there's only
        // one root and it has no parent index. Callers wanting the root
        // use add_block(kind, invalid_block_id).
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::insert_block: parent must be a real block; "
                              "use add_block(kind, invalid_block_id) to create the root"));
    }
    auto parent_it = nodes_.find(parent);
    if (parent_it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::insert_block: unknown parent " + std::to_string(parent)));
    }
    auto& children = parent_it->second.children;
    if (index > children.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::insert_block: index " + std::to_string(index) +
                                  " out of range (size " + std::to_string(children.size()) + ")"));
    }
    const auto id = next_id_++;
    BlockNode node{};
    node.id = id;
    node.kind = kind;
    node.name = std::move(name);
    node.parent = parent;
    node.payload = default_payload_for(kind);
    nodes_.emplace(id, std::move(node));
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(index), id);
    return id;
}

auto Document::remove_block(BlockId id) -> Result<void> {
    if (id == invalid_block_id) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "Document::remove_block: id is invalid_block_id"));
    }
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::remove_block: unknown id " + std::to_string(id)));
    }
    if (id == root_ && !it->second.children.empty()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "Document::remove_block: cannot remove root with children; call clear() to wipe"));
    }

    // Collect the full subtree first so the iteration is decoupled
    // from the mutation. Order is pre-order — we erase children before
    // their parents to keep validate() happy if it were called mid-loop.
    std::vector<BlockId> subtree;
    collect_subtree_(id, subtree);

    // Detach from parent's children list. For the root this is a no-op
    // because the root has no parent.
    if (id != root_) {
        (void) detach_from_parent_(id);
    } else {
        root_ = invalid_block_id;
    }

    for (const auto bid : subtree) {
        nodes_.erase(bid);
    }
    return {};
}

auto Document::restore_subtree(std::vector<BlockNode> subtree,
                               std::size_t insert_index) -> Result<void> {
    if (subtree.empty()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "Document::restore_subtree: subtree is empty"));
    }
    // ---- Validation pass — no mutation until every check passes. ----------
    const BlockId destination_parent = subtree.front().parent;
    const bool restoring_root = (destination_parent == invalid_block_id);
    if (restoring_root) {
        // Document-root restoration: document must currently have no
        // root. (The root was removed by remove_block, which only
        // succeeds when root is childless — so this case is the precise
        // inverse of that.)
        if (root_ != invalid_block_id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::restore_subtree: cannot restore a root while one already exists"));
        }
    } else {
        const auto parent_it = nodes_.find(destination_parent);
        if (parent_it == nodes_.end()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "Document::restore_subtree: destination parent " +
                                      std::to_string(destination_parent) + " not in document"));
        }
        if (insert_index > parent_it->second.children.size()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "Document::restore_subtree: insert_index " +
                                      std::to_string(insert_index) + " out of range (size " +
                                      std::to_string(parent_it->second.children.size()) + ")"));
        }
    }

    // Every id is fresh; every non-root parent points at an earlier
    // node in the list; every child appears exactly once in someone's
    // children list inside the subtree.
    std::unordered_set<BlockId> known_ids;
    known_ids.reserve(subtree.size());
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        const auto& node = subtree[i];
        if (node.id == invalid_block_id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                "Document::restore_subtree: subtree contains an invalid_block_id node"));
        }
        if (nodes_.find(node.id) != nodes_.end()) {
            return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                     "Document::restore_subtree: id " +
                                                         std::to_string(node.id) +
                                                         " is already present in the document"));
        }
        if (!known_ids.insert(node.id).second) {
            return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                     "Document::restore_subtree: duplicate id " +
                                                         std::to_string(node.id) +
                                                         " inside the subtree"));
        }
        if (i == 0) {
            continue;  // root: its parent is in the live document, validated above
        }
        // Non-root nodes: parent must be an earlier node in the list.
        if (known_ids.find(node.parent) == known_ids.end()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::restore_subtree: node " + std::to_string(node.id) + " has parent " +
                    std::to_string(node.parent) + " which is not earlier in the subtree"));
        }
    }
    // Children references must be consistent with parent references.
    for (const auto& node : subtree) {
        std::unordered_set<BlockId> seen;
        for (const auto child : node.children) {
            if (known_ids.find(child) == known_ids.end()) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "Document::restore_subtree: node " + std::to_string(node.id) +
                        " references child " + std::to_string(child) + " not in the subtree"));
            }
            if (!seen.insert(child).second) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "Document::restore_subtree: node " + std::to_string(node.id) + " lists child " +
                        std::to_string(child) + " more than once"));
            }
        }
    }

    // ---- Mutation pass — every check above has passed. -------------------
    const BlockId root_id = subtree.front().id;
    BlockId max_seen = 0;
    for (auto& node : subtree) {
        max_seen = std::max(max_seen, node.id);
        nodes_.emplace(node.id, std::move(node));
    }
    if (restoring_root) {
        root_ = root_id;
    } else {
        // parent_it from the validation block is gone now; re-lookup
        // is cheap and avoids holding a reference across the mutation.
        nodes_.at(destination_parent)
            .children.insert(nodes_.at(destination_parent).children.begin() +
                                 static_cast<std::ptrdiff_t>(insert_index),
                             root_id);
    }
    if (max_seen >= next_id_) {
        next_id_ = max_seen + 1;
    }
    return {};
}

void Document::clear() noexcept {
    nodes_.clear();
    root_ = invalid_block_id;
    pages_ = noted::canvas::PageList{};
    shapes_.clear();
    // next_id_ is intentionally NOT reset — IDs stay monotonic across
    // clears so any history / undo references survive the wipe.
}

auto Document::add_page(float w,
                        float h,
                        noted::canvas::PageBackground bg,
                        float origin_x) -> Result<std::size_t> {
    const auto idx = pages_.add_page(w, h, bg);
    pages_.set_page_origin_x(idx, origin_x);
    return idx;
}

auto Document::remove_page(std::size_t index) -> Result<void> {
    if (index >= pages_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::remove_page: index " + std::to_string(index) +
                                  " out of range (size " + std::to_string(pages_.size()) + ")"));
    }
    pages_.remove_page(index);
    return {};
}

auto Document::insert_page(std::size_t index,
                           const noted::canvas::Page& page) -> Result<std::size_t> {
    if (index > pages_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::insert_page: index " + std::to_string(index) +
                                  " out of range (size " + std::to_string(pages_.size()) + ")"));
    }
    return pages_.insert_page(index, page);
}

void Document::replace_pages(noted::canvas::PageList pages) noexcept {
    pages_ = std::move(pages);
}

auto Document::add_shape(noted::domain::tool::ShapePrimitive shape) -> Result<std::size_t> {
    shapes_.push_back(std::move(shape));
    return shapes_.size() - 1;
}

auto Document::remove_shape(std::size_t index) -> Result<void> {
    if (index >= shapes_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::remove_shape: index " + std::to_string(index) +
                                  " out of range (size " + std::to_string(shapes_.size()) + ")"));
    }
    shapes_.erase(shapes_.begin() + static_cast<std::ptrdiff_t>(index));
    return {};
}

auto Document::insert_shape(std::size_t index,
                            noted::domain::tool::ShapePrimitive shape) -> Result<std::size_t> {
    if (index > shapes_.size()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::insert_shape: index " + std::to_string(index) +
                                  " out of range (size " + std::to_string(shapes_.size()) + ")"));
    }
    shapes_.insert(shapes_.begin() + static_cast<std::ptrdiff_t>(index), std::move(shape));
    return index;
}

void Document::replace_shapes(std::vector<noted::domain::tool::ShapePrimitive> shapes) noexcept {
    shapes_ = std::move(shapes);
}

auto Document::move_to(BlockId id, BlockId new_parent, std::size_t index) -> Result<void> {
    if (id == invalid_block_id || new_parent == invalid_block_id) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::move_to: id or new_parent is invalid_block_id"));
    }
    auto node_it = nodes_.find(id);
    auto parent_it = nodes_.find(new_parent);
    if (node_it == nodes_.end() || parent_it == nodes_.end()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "Document::move_to: unknown id or new_parent"));
    }
    if (id == root_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "Document::move_to: cannot re-parent the root"));
    }
    if (new_parent == id || is_ancestor_of_(id, new_parent)) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "Document::move_to: new_parent is " + std::to_string(id) + " or a descendant"));
    }
    if (index > parent_it->second.children.size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "Document::move_to: index " + std::to_string(index) + " out of range (size " +
                std::to_string(parent_it->second.children.size()) + ")"));
    }

    // No-op short-circuit: moving to its current position is a success
    // with zero mutation.
    if (node_it->second.parent == new_parent) {
        auto& siblings = parent_it->second.children;
        const auto pos = std::find(siblings.begin(), siblings.end(), id);
        if (pos != siblings.end() && static_cast<std::size_t>(pos - siblings.begin()) == index) {
            return {};
        }
    }

    // Detach, then re-attach. Both legs touch only validated state,
    // so no rollback is needed.
    (void) detach_from_parent_(id);
    node_it->second.parent = new_parent;
    parent_it->second.children.insert(
        parent_it->second.children.begin() + static_cast<std::ptrdiff_t>(index), id);
    return {};
}

auto Document::set_payload(BlockId id, BlockPayload payload) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::set_payload: unknown id " + std::to_string(id)));
    }
    if (kind_of(payload) != it->second.kind) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "Document::set_payload: payload kind does not match block kind for id " +
                std::to_string(id)));
    }
    // Clamp the heading level on the way in so callers can't get a 0 or 7+.
    if (auto* heading = std::get_if<HeadingPayload>(&payload); heading != nullptr) {
        heading->level = clamp_heading_level(heading->level);
    }
    it->second.payload = std::move(payload);
    return {};
}

auto Document::set_visible(BlockId id, bool v) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::set_visible: unknown id " + std::to_string(id)));
    }
    it->second.visible = v;
    return {};
}

auto Document::set_name(BlockId id, std::string name) -> Result<void> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::set_name: unknown id " + std::to_string(id)));
    }
    it->second.name = std::move(name);
    return {};
}

auto Document::find(BlockId id) const noexcept -> const BlockNode* {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

auto Document::preorder(BlockId start) const -> Result<std::vector<BlockId>> {
    const BlockId begin = (start == invalid_block_id) ? root_ : start;
    if (begin == invalid_block_id) {
        return std::vector<BlockId>{};  // empty document → empty traversal
    }
    if (nodes_.find(begin) == nodes_.end()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "Document::preorder: unknown start " + std::to_string(begin)));
    }
    std::vector<BlockId> out;
    out.reserve(nodes_.size());
    collect_subtree_(begin, out);
    return out;
}

auto Document::validate() const -> Result<void> {
    if (root_ != invalid_block_id) {
        const auto root_it = nodes_.find(root_);
        if (root_it == nodes_.end()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::validate: root " + std::to_string(root_) + " is not in the document"));
        }
        if (root_it->second.parent != invalid_block_id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::validate: root " + std::to_string(root_) + " has non-zero parent " +
                    std::to_string(root_it->second.parent)));
        }
    }

    // Each block appears in at most one parent's children list. We
    // track the parent of every child reference and compare to the
    // node's own parent pointer.
    std::unordered_map<BlockId, BlockId> reported_parent;
    reported_parent.reserve(nodes_.size());
    for (const auto& [id, node] : nodes_) {
        std::unordered_set<BlockId> seen_here;
        for (const auto child : node.children) {
            if (child == invalid_block_id || nodes_.find(child) == nodes_.end()) {
                return std::unexpected(
                    noted::make_error(noted::ErrorCode::invalid_state,
                                      "Document::validate: node " + std::to_string(id) +
                                          " has unknown child " + std::to_string(child)));
            }
            if (!seen_here.insert(child).second) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "Document::validate: node " + std::to_string(id) + " lists child " +
                        std::to_string(child) + " more than once"));
            }
            auto [it, inserted] = reported_parent.emplace(child, id);
            if (!inserted) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_state,
                    "Document::validate: child " + std::to_string(child) + " referenced by both " +
                        std::to_string(it->second) + " and " + std::to_string(id)));
            }
        }
    }
    // Parent ↔ children agreement and connectivity from the root.
    for (const auto& [id, node] : nodes_) {
        if (id == root_) {
            continue;  // root is the one node with no reverse mapping
        }
        const auto rp_it = reported_parent.find(id);
        if (rp_it == reported_parent.end()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_state,
                                  "Document::validate: node " + std::to_string(id) +
                                      " is not listed as a child by any parent"));
        }
        if (rp_it->second != node.parent) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_state,
                                  "Document::validate: node " + std::to_string(id) +
                                      " has parent " + std::to_string(node.parent) +
                                      " but is listed under " + std::to_string(rp_it->second)));
        }
    }

    // Connectivity: every node must be reachable from root via children.
    if (root_ != invalid_block_id) {
        std::unordered_set<BlockId> reachable;
        std::vector<BlockId> stack;
        stack.push_back(root_);
        while (!stack.empty()) {
            const auto top = stack.back();
            stack.pop_back();
            if (!reachable.insert(top).second) {
                continue;
            }
            const auto it = nodes_.find(top);
            if (it == nodes_.end()) {
                continue;  // already reported above
            }
            for (const auto child : it->second.children) {
                stack.push_back(child);
            }
        }
        if (reachable.size() != nodes_.size()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "Document::validate: " + std::to_string(nodes_.size() - reachable.size()) +
                    " block(s) are not reachable from root"));
        }
    } else if (!nodes_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "Document::validate: nodes exist but root is invalid_block_id"));
    }
    return {};
}

auto Document::detach_from_parent_(BlockId id) -> std::pair<BlockId, std::size_t> {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return {invalid_block_id, 0};
    }
    const BlockId parent = it->second.parent;
    if (parent == invalid_block_id) {
        return {invalid_block_id, 0};
    }
    auto parent_it = nodes_.find(parent);
    if (parent_it == nodes_.end()) {
        return {invalid_block_id, 0};
    }
    auto& siblings = parent_it->second.children;
    const auto pos = std::find(siblings.begin(), siblings.end(), id);
    if (pos == siblings.end()) {
        return {parent, 0};
    }
    const auto index = static_cast<std::size_t>(pos - siblings.begin());
    siblings.erase(pos);
    it->second.parent = invalid_block_id;
    return {parent, index};
}

auto Document::is_ancestor_of_(BlockId ancestor, BlockId descendant) const noexcept -> bool {
    BlockId cursor = descendant;
    while (cursor != invalid_block_id) {
        const auto it = nodes_.find(cursor);
        if (it == nodes_.end()) {
            return false;
        }
        if (it->second.parent == ancestor) {
            return true;
        }
        cursor = it->second.parent;
    }
    return false;
}

void Document::collect_subtree_(BlockId start, std::vector<BlockId>& out) const {
    // Iterative pre-order so deep trees don't blow the C++ stack.
    std::vector<BlockId> stack;
    stack.push_back(start);
    while (!stack.empty()) {
        const auto id = stack.back();
        stack.pop_back();
        const auto it = nodes_.find(id);
        if (it == nodes_.end()) {
            continue;
        }
        out.push_back(id);
        // Push children in reverse so the first child is processed first.
        const auto& children = it->second.children;
        for (auto rit = children.rbegin(); rit != children.rend(); ++rit) {
            stack.push_back(*rit);
        }
    }
}

}  // namespace noted::domain
