#pragma once

// Document — the project's unified block tree.
//
// A Document is a strict tree of `BlockNode`s. Each block has a kind
// (group / text / heading / code / canvas / image / embed), an
// ordered list of children, an optional name, a visibility flag, and
// a kind-specific payload. The root node is the document itself —
// typically a `group`.
//
// Why one tree for notes AND image edits:
//   - A "notebook" is a `group` of pages.
//   - A "page" is a `group` of blocks.
//   - Mixing a heading + paragraph + a Goodnotes-style `canvas` + a
//     Photoshop-style `image` inside one page is the product's
//     differentiator. Forcing two parallel data structures (one for
//     each domain) would multiply every cross-cutting feature: undo,
//     CRDT replication, search, autosave, file format.
//
// Heavy data lives in side-stores, referenced by opaque IDs:
//   - `CanvasPayload::graph_id` and `ImagePayload::edit_graph_id`
//     refer to entries in an external `LayerGraph` store.
//   - `ImagePayload::asset_id` refers to the asset registry holding
//     the actual bitmap bytes.
//   This keeps `Document` cheap to clone, diff, and serialize.
//
// Pure data — no GPU, no I/O, no platform. All mutators return
// `Result<T>`; failures leave the document untouched
// (validate-then-mutate, mirroring `LayerGraph`).
//
// Rationale: see docs/architecture/0023-document-block-tree.md.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::domain {

using BlockId = std::uint64_t;
inline constexpr BlockId invalid_block_id = 0;

// Side-store identifiers. The `Document` only stores the ID; the host
// application (or the LayerGraph store / AssetRegistry) owns the
// referenced bytes.
using AssetId = std::uint64_t;
inline constexpr AssetId invalid_asset_id = 0;

using LayerGraphId = std::uint64_t;
inline constexpr LayerGraphId invalid_layer_graph_id = 0;

// Wire-stable numeric values. New kinds append, never reorder —
// `.noted` file format depends on the integer values.
enum class BlockKind : std::uint8_t {
    group = 0,    // structural container: notebooks, pages, sections, columns
    text = 1,     // plain paragraph (rich-text format TBD)
    heading = 2,  // header with a level (1..6, like HTML)
    code = 3,     // monospace block with an optional language hint
    canvas = 4,   // ink canvas — references a LayerGraph (Goodnotes side)
    image = 5,    // raster image with non-destructive edit graph (Photoshop side)
    embed = 6,    // external link / iframe / uri
};

// ---- Payloads --------------------------------------------------------------

// `group` carries no extra data — children + name + visibility tell the
// whole story. Kept as a tag in the variant for symmetry and so
// switching to a child-bearing group payload later doesn't break the
// type.
struct GroupPayload {
    [[nodiscard]] auto operator==(const GroupPayload&) const noexcept -> bool = default;
};

struct TextPayload {
    std::string content;
    [[nodiscard]] auto operator==(const TextPayload&) const noexcept -> bool = default;
};

struct HeadingPayload {
    std::string content;
    std::uint8_t level{1};  // 1..6 (clamped on set)
    [[nodiscard]] auto operator==(const HeadingPayload&) const noexcept -> bool = default;
};

struct CodePayload {
    std::string content;
    std::string language;  // free-form hint; renderer picks a highlighter
    [[nodiscard]] auto operator==(const CodePayload&) const noexcept -> bool = default;
};

struct CanvasPayload {
    LayerGraphId graph_id{invalid_layer_graph_id};
    std::uint32_t width{0};
    std::uint32_t height{0};
    [[nodiscard]] auto operator==(const CanvasPayload&) const noexcept -> bool = default;
};

struct ImagePayload {
    AssetId asset_id{invalid_asset_id};
    LayerGraphId edit_graph_id{invalid_layer_graph_id};
    [[nodiscard]] auto operator==(const ImagePayload&) const noexcept -> bool = default;
};

struct EmbedPayload {
    std::string uri;
    [[nodiscard]] auto operator==(const EmbedPayload&) const noexcept -> bool = default;
};

using BlockPayload = std::variant<GroupPayload,
                                  TextPayload,
                                  HeadingPayload,
                                  CodePayload,
                                  CanvasPayload,
                                  ImagePayload,
                                  EmbedPayload>;

// Construct the default payload for a `kind`. Useful for `add_block`
// which has no payload parameter — the caller can `set_payload` later.
// The kind→variant mapping is 1:1 and pinned: changing this is a
// breaking on-disk format change.
[[nodiscard]] auto default_payload_for(BlockKind kind) -> BlockPayload;

// Returns the BlockKind that matches the active alternative in the
// variant. Inverse of `default_payload_for` (modulo concrete data).
[[nodiscard]] auto kind_of(const BlockPayload& payload) noexcept -> BlockKind;

// ---- BlockNode -------------------------------------------------------------

struct BlockNode {
    BlockId id{invalid_block_id};
    BlockKind kind{BlockKind::text};
    bool visible{true};
    std::string name;
    BlockId parent{invalid_block_id};  // invalid_block_id when this is the root
    std::vector<BlockId> children;     // ordered; matters for render / display
    BlockPayload payload{TextPayload{}};
};

// ---- Document --------------------------------------------------------------

// Strict tree of `BlockNode`s. Mirrors `LayerGraph`'s mutation contract:
//
//   - IDs are monotonically allocated; `invalid_block_id` (0) is reserved.
//   - Mutators return `Result<T>` and never leave the document in a
//     partially-modified state on failure.
//   - `validate()` is O(N) and catches drift between the parent /
//     children mirrors (the two halves of the tree representation).
//
// Why both `parent` and `children` are stored:
//   - Children order is rendering / display order — essential.
//   - Parent pointer makes upward traversal (cycle check on move,
//     breadcrumb UI, undo's "where did this come from") O(1).
//   - The redundancy is a contract: every mutator updates both sides
//     atomically; `validate()` proves they stayed in sync.
class Document {
public:
    Document() = default;
    Document(const Document&) = default;
    auto operator=(const Document&) -> Document& = default;
    Document(Document&&) noexcept = default;
    auto operator=(Document&&) noexcept -> Document& = default;
    ~Document() = default;

    // Allocate a block and append it to `parent`'s children list.
    //
    // Special case: when the document has no root, calling with
    // `parent == invalid_block_id` allocates the root. Subsequent
    // attempts to add another root return invalid_state.
    //
    // The new block's payload is `default_payload_for(kind)`; the
    // caller can `set_payload()` to fill in concrete data.
    [[nodiscard]] auto add_block(BlockKind kind,
                                 BlockId parent,
                                 std::string name = {}) -> Result<BlockId>;

    // Same as add_block, but insert at a specific position in
    // `parent`'s children list. `index == children.size()` appends.
    [[nodiscard]] auto insert_block(BlockKind kind,
                                    BlockId parent,
                                    std::size_t index,
                                    std::string name = {}) -> Result<BlockId>;

    // Remove a block AND its entire subtree. Detaches from parent's
    // children list. Rejects:
    //   - invalid_argument: id is unknown.
    //   - invalid_state:    id is the root and the root has children
    //                       (use clear() for a full wipe).
    auto remove_block(BlockId id) -> Result<void>;

    // Drop every block. Resets root to invalid_block_id. Next id stays
    // monotonic across clears so IDs from history stay distinct.
    void clear() noexcept;

    // Re-parent `id` (and its subtree) under `new_parent` at the
    // given `index` in new_parent's children list. Rejects:
    //   - invalid_argument: id / new_parent unknown, or index OOB.
    //   - invalid_state:    new_parent is `id` itself or a descendant
    //                       of `id` (would create a cycle).
    //   - invalid_state:    `id` is the root (root cannot be re-parented).
    auto move_to(BlockId id, BlockId new_parent, std::size_t index) -> Result<void>;

    // Field mutators. Each rejects with invalid_argument on unknown id.
    auto set_payload(BlockId id, BlockPayload payload) -> Result<void>;
    auto set_visible(BlockId id, bool v) -> Result<void>;
    auto set_name(BlockId id, std::string name) -> Result<void>;

    // Lookup. Returns nullptr if id is unknown.
    [[nodiscard]] auto find(BlockId id) const noexcept -> const BlockNode*;

    [[nodiscard]] auto root() const noexcept -> BlockId { return root_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return nodes_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return nodes_.empty(); }

    // Pre-order traversal of the subtree rooted at `start`. If
    // `start == invalid_block_id`, the document root is used; on an
    // empty document the result is an empty vector. Errors with
    // invalid_argument when `start` is unknown.
    [[nodiscard]] auto preorder(BlockId start = invalid_block_id) const
        -> Result<std::vector<BlockId>>;

    // Whole-document well-formedness. Checks:
    //   - Root presence: if root_ is set, it must be in the map and have
    //     parent == invalid_block_id.
    //   - Parent ↔ children consistency: every node's parent reference
    //     must agree with its parent's children list; every children
    //     entry must be a known block; no block appears in two children
    //     lists; no duplicate within a single list.
    //   - Connectivity: every block is reachable from the root via the
    //     children pointer chain (no orphans).
    // O(N). The compositor / file-format code calls this before commit.
    [[nodiscard]] auto validate() const -> Result<void>;

private:
    // Detach `id` from its parent's children list, leaving the node
    // itself otherwise intact. Returns the (parent, index) it was
    // located at, useful for rollback in `move_to`.
    auto detach_from_parent_(BlockId id) -> std::pair<BlockId, std::size_t>;

    // Walk the parent-pointer chain up from `descendant`; return true
    // if `ancestor` is encountered. Used for move_to cycle detection.
    [[nodiscard]] auto is_ancestor_of_(BlockId ancestor, BlockId descendant) const noexcept -> bool;

    // Recursive subtree collection — used by remove_block and preorder.
    void collect_subtree_(BlockId start, std::vector<BlockId>& out) const;

    std::unordered_map<BlockId, BlockNode> nodes_;
    BlockId root_{invalid_block_id};
    BlockId next_id_{1};  // 0 is reserved
};

}  // namespace noted::domain
