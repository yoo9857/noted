#include "noted/domain/io/document_json.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "noted/domain/document/document.hpp"

namespace noted::domain::io {

namespace {

using json = nlohmann::json;

// Top-level keys we recognize. Strict parser rejects any other key.
// `pages` is optional for v1 (back-compat) — its presence is not
// itself an error at any version.
constexpr std::array<std::string_view, 4> kTopLevelKeys{"version", "root", "blocks", "pages"};

// Per-pages-object keys.
constexpr std::array<std::string_view, 2> kPagesKeys{"gap_px", "items"};

// Per-page-item keys.
constexpr std::array<std::string_view, 4> kPageItemKeys{"w", "h", "bg", "x"};

// Per-block keys we recognize.
constexpr std::array<std::string_view, 7> kBlockKeys{
    "id", "kind", "visible", "name", "parent", "children", "payload"};

// Payload-key tables per kind. Empty kinds (group) accept no fields.
constexpr std::array<std::string_view, 0> kGroupKeys{};
constexpr std::array<std::string_view, 1> kTextKeys{"content"};
constexpr std::array<std::string_view, 2> kHeadingKeys{"content", "level"};
constexpr std::array<std::string_view, 2> kCodeKeys{"content", "language"};
constexpr std::array<std::string_view, 3> kCanvasKeys{"graph_id", "width", "height"};
constexpr std::array<std::string_view, 2> kImageKeys{"asset_id", "edit_graph_id"};
constexpr std::array<std::string_view, 1> kEmbedKeys{"uri"};

template <std::size_t N>
[[nodiscard]] auto contains(const std::array<std::string_view, N>& haystack,
                            std::string_view needle) noexcept -> bool {
    for (const auto& s : haystack) {
        if (s == needle) {
            return true;
        }
    }
    return false;
}

// Reject unknown keys in `obj`. Returns the offending key name on
// failure (caller wraps with context).
[[nodiscard]] auto find_unknown_key(const json& obj, const auto& allowed) -> std::string {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!contains(allowed, it.key())) {
            return it.key();
        }
    }
    return {};
}

[[nodiscard]] auto payload_keys_for(BlockKind kind) noexcept -> std::vector<std::string_view> {
    switch (kind) {
        case BlockKind::group:
            return {kGroupKeys.begin(), kGroupKeys.end()};
        case BlockKind::text:
            return {kTextKeys.begin(), kTextKeys.end()};
        case BlockKind::heading:
            return {kHeadingKeys.begin(), kHeadingKeys.end()};
        case BlockKind::code:
            return {kCodeKeys.begin(), kCodeKeys.end()};
        case BlockKind::canvas:
            return {kCanvasKeys.begin(), kCanvasKeys.end()};
        case BlockKind::image:
            return {kImageKeys.begin(), kImageKeys.end()};
        case BlockKind::embed:
            return {kEmbedKeys.begin(), kEmbedKeys.end()};
    }
    return {};
}

// ---- Serialization (Document → json::object) ----------------------------

[[nodiscard]] auto serialize_payload(const BlockPayload& p) -> json {
    return std::visit(
        [](const auto& v) -> json {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, GroupPayload>) {
                return json::object();
            } else if constexpr (std::is_same_v<T, TextPayload>) {
                return json{{"content", v.content}};
            } else if constexpr (std::is_same_v<T, HeadingPayload>) {
                return json{{"content", v.content}, {"level", v.level}};
            } else if constexpr (std::is_same_v<T, CodePayload>) {
                return json{{"content", v.content}, {"language", v.language}};
            } else if constexpr (std::is_same_v<T, CanvasPayload>) {
                return json{{"graph_id", v.graph_id}, {"width", v.width}, {"height", v.height}};
            } else if constexpr (std::is_same_v<T, ImagePayload>) {
                return json{{"asset_id", v.asset_id}, {"edit_graph_id", v.edit_graph_id}};
            } else {
                static_assert(std::is_same_v<T, EmbedPayload>,
                              "unhandled BlockPayload alternative");
                return json{{"uri", v.uri}};
            }
        },
        p);
}

[[nodiscard]] auto serialize_block(const BlockNode& node) -> json {
    return json{
        {"id", node.id},
        {"kind", static_cast<int>(node.kind)},
        {"visible", node.visible},
        {"name", node.name},
        {"parent", node.parent},
        {"children", node.children},
        {"payload", serialize_payload(node.payload)},
    };
}

// ---- Deserialization (json → BlockNode) ---------------------------------

template <typename T>
[[nodiscard]] auto require(const json& obj,
                           std::string_view key,
                           std::string_view context) -> Result<T> {
    if (!obj.contains(key)) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            std::string{context} + ": missing required key '" + std::string{key} + "'"));
    }
    try {
        return obj.at(key).get<T>();
    } catch (const json::exception& e) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 std::string{context} + ": key '" +
                                                     std::string{key} + "' has wrong type — " +
                                                     e.what()));
    }
}

[[nodiscard]] auto parse_payload(const json& payload,
                                 BlockKind kind,
                                 std::string_view context) -> Result<BlockPayload> {
    if (!payload.is_object()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              std::string{context} + ": payload is not an object"));
    }
    // Strict-mode key check.
    const auto allowed = payload_keys_for(kind);
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        bool found = false;
        for (const auto& a : allowed) {
            if (a == it.key()) {
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                std::string{context} + ": payload has unknown key '" + it.key() + "'"));
        }
    }

    switch (kind) {
        case BlockKind::group:
            return GroupPayload{};
        case BlockKind::text: {
            auto content = require<std::string>(payload, "content", context);
            if (!content) {
                return std::unexpected(std::move(content).error());
            }
            return TextPayload{.content = std::move(*content)};
        }
        case BlockKind::heading: {
            auto content = require<std::string>(payload, "content", context);
            auto level = require<int>(payload, "level", context);
            if (!content) {
                return std::unexpected(std::move(content).error());
            }
            if (!level) {
                return std::unexpected(std::move(level).error());
            }
            HeadingPayload h{.content = std::move(*content),
                             .level = static_cast<std::uint8_t>(std::clamp(*level, 1, 6))};
            return h;
        }
        case BlockKind::code: {
            auto content = require<std::string>(payload, "content", context);
            auto language = require<std::string>(payload, "language", context);
            if (!content) {
                return std::unexpected(std::move(content).error());
            }
            if (!language) {
                return std::unexpected(std::move(language).error());
            }
            return CodePayload{.content = std::move(*content), .language = std::move(*language)};
        }
        case BlockKind::canvas: {
            auto graph_id = require<LayerGraphId>(payload, "graph_id", context);
            auto width = require<std::uint32_t>(payload, "width", context);
            auto height = require<std::uint32_t>(payload, "height", context);
            if (!graph_id) {
                return std::unexpected(std::move(graph_id).error());
            }
            if (!width) {
                return std::unexpected(std::move(width).error());
            }
            if (!height) {
                return std::unexpected(std::move(height).error());
            }
            return CanvasPayload{.graph_id = *graph_id, .width = *width, .height = *height};
        }
        case BlockKind::image: {
            auto asset_id = require<AssetId>(payload, "asset_id", context);
            auto edit_graph_id = require<LayerGraphId>(payload, "edit_graph_id", context);
            if (!asset_id) {
                return std::unexpected(std::move(asset_id).error());
            }
            if (!edit_graph_id) {
                return std::unexpected(std::move(edit_graph_id).error());
            }
            return ImagePayload{.asset_id = *asset_id, .edit_graph_id = *edit_graph_id};
        }
        case BlockKind::embed: {
            auto uri = require<std::string>(payload, "uri", context);
            if (!uri) {
                return std::unexpected(std::move(uri).error());
            }
            return EmbedPayload{.uri = std::move(*uri)};
        }
    }
    return std::unexpected(
        noted::make_error(noted::ErrorCode::invalid_argument,
                          std::string{context} + ": unrecognized kind value (out of range)"));
}

[[nodiscard]] auto parse_block(const json& obj, std::size_t index) -> Result<BlockNode> {
    const std::string context = "blocks[" + std::to_string(index) + "]";

    // Strict-mode key check.
    if (auto bad = find_unknown_key(obj, kBlockKeys); !bad.empty()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 context + ": unknown key '" + bad + "'"));
    }

    BlockNode node{};

    auto id = require<BlockId>(obj, "id", context);
    if (!id) {
        return std::unexpected(std::move(id).error());
    }
    if (*id == invalid_block_id) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 context + ": id is invalid_block_id"));
    }
    node.id = *id;

    auto kind_int = require<int>(obj, "kind", context);
    if (!kind_int) {
        return std::unexpected(std::move(kind_int).error());
    }
    if (*kind_int < 0 || *kind_int > static_cast<int>(BlockKind::embed)) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            context + ": kind ordinal " + std::to_string(*kind_int) + " out of range"));
    }
    node.kind = static_cast<BlockKind>(*kind_int);

    auto visible = require<bool>(obj, "visible", context);
    if (!visible) {
        return std::unexpected(std::move(visible).error());
    }
    node.visible = *visible;

    auto name = require<std::string>(obj, "name", context);
    if (!name) {
        return std::unexpected(std::move(name).error());
    }
    node.name = std::move(*name);

    auto parent = require<BlockId>(obj, "parent", context);
    if (!parent) {
        return std::unexpected(std::move(parent).error());
    }
    node.parent = *parent;

    auto children = require<std::vector<BlockId>>(obj, "children", context);
    if (!children) {
        return std::unexpected(std::move(children).error());
    }
    node.children = std::move(*children);

    if (!obj.contains("payload")) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 context + ": missing required key 'payload'"));
    }
    auto payload = parse_payload(obj.at("payload"), node.kind, context);
    if (!payload) {
        return std::unexpected(std::move(payload).error());
    }
    node.payload = std::move(*payload);

    return node;
}

}  // namespace

[[nodiscard]] auto serialize_pages(const noted::canvas::PageList& pages) -> json {
    json items = json::array();
    for (const auto& p : pages.pages()) {
        items.push_back({
            {"w", p.extent_w_px},
            {"h", p.extent_h_px},
            {"bg", static_cast<int>(p.background)},
            {"x", p.origin_x_px},
        });
    }
    return json{
        {"gap_px", pages.gap_px()},
        {"items", std::move(items)},
    };
}

auto document_to_json(const Document& doc) -> std::string {
    json out;
    out["version"] = kDocumentJsonVersion;
    out["root"] = doc.root();

    json blocks = json::array();
    auto order = doc.preorder();
    if (order) {
        for (const auto id : *order) {
            const auto* node = doc.find(id);
            if (node == nullptr) {
                continue;  // shouldn't happen; preorder() only returns known ids
            }
            blocks.push_back(serialize_block(*node));
        }
    }
    out["blocks"] = std::move(blocks);
    out["pages"] = serialize_pages(doc.pages());

    // 2-space indent — readable diffs at small document scale.
    return out.dump(2);
}

auto document_from_json(std::string_view json_text) -> Result<Document> {
    json root_obj;
    try {
        root_obj = json::parse(json_text);
    } catch (const json::parse_error& e) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              std::string{"document_from_json: malformed JSON — "} + e.what()));
    }
    if (!root_obj.is_object()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "document_from_json: root is not an object"));
    }

    // Strict-mode top-level key check.
    if (auto bad = find_unknown_key(root_obj, kTopLevelKeys); !bad.empty()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            std::string{"document_from_json: unknown top-level key '"} + bad + "'"));
    }

    // Version. Reader accepts every version in
    // [kDocumentJsonMinReadableVersion, kDocumentJsonVersion]. A
    // newer version on disk is a hard error (asks the user to
    // upgrade the binary) — silent truncation of unknown fields
    // would risk data loss on round-trip.
    auto version = require<int>(root_obj, "version", "document_from_json");
    if (!version) {
        return std::unexpected(std::move(version).error());
    }
    if (*version < kDocumentJsonMinReadableVersion || *version > kDocumentJsonVersion) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "document_from_json: unsupported version " + std::to_string(*version) +
                " (supported [" + std::to_string(kDocumentJsonMinReadableVersion) + ", " +
                std::to_string(kDocumentJsonVersion) + "])"));
    }

    auto stored_root = require<BlockId>(root_obj, "root", "document_from_json");
    if (!stored_root) {
        return std::unexpected(std::move(stored_root).error());
    }

    if (!root_obj.contains("blocks")) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "document_from_json: missing 'blocks'"));
    }
    const auto& blocks_arr = root_obj.at("blocks");
    if (!blocks_arr.is_array()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "document_from_json: 'blocks' is not an array"));
    }

    Document doc;
    if (blocks_arr.empty()) {
        // Empty-blocks invariant: root must also be invalid. Fall
        // through to the pages parse step (a v2 doc can have empty
        // blocks but populated pages — and the strict-key checks for
        // the pages object must still run even on an empty-block
        // doc, otherwise unknown keys silently round-trip).
        if (*stored_root != invalid_block_id) {
            return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                     "document_from_json: root id " +
                                                         std::to_string(*stored_root) +
                                                         " set but blocks is empty"));
        }
    } else {
        // Parse blocks into a vector<BlockNode>. The file is expected to be
        // in pre-order (root first, parents before children) — `restore_subtree`
        // enforces that, plus internal consistency.
        std::vector<BlockNode> subtree;
        subtree.reserve(blocks_arr.size());
        for (std::size_t i = 0; i < blocks_arr.size(); ++i) {
            auto node = parse_block(blocks_arr[i], i);
            if (!node) {
                return std::unexpected(std::move(node).error());
            }
            subtree.push_back(std::move(*node));
        }

        // The first block must be the document root. Verify its id matches
        // the file's stored root id.
        if (subtree.front().id != *stored_root) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "document_from_json: blocks[0].id " + std::to_string(subtree.front().id) +
                    " does not match top-level root " + std::to_string(*stored_root)));
        }
        if (subtree.front().parent != invalid_block_id) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "document_from_json: root block must have parent=0 (invalid_block_id)"));
        }

        auto restore = doc.restore_subtree(std::move(subtree), 0);
        if (!restore) {
            return std::unexpected(std::move(restore).error());
        }
    }

    // Pages — optional at any version. Absent ⇒ empty page list.
    if (root_obj.contains("pages")) {
        const auto& pages_obj = root_obj.at("pages");
        if (!pages_obj.is_object()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'pages' is not an object"));
        }
        if (auto bad = find_unknown_key(pages_obj, kPagesKeys); !bad.empty()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'pages' has unknown key '" + bad + "'"));
        }
        auto gap = require<float>(pages_obj, "gap_px", "pages");
        if (!gap) {
            return std::unexpected(std::move(gap).error());
        }
        if (!pages_obj.contains("items")) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument, "document_from_json: 'pages' missing 'items'"));
        }
        const auto& items = pages_obj.at("items");
        if (!items.is_array()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'pages.items' is not an array"));
        }
        noted::canvas::PageList pages;
        pages.set_gap_px(*gap);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string context = "pages.items[" + std::to_string(i) + "]";
            const auto& item = items[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kPageItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }
            auto w = require<float>(item, "w", context);
            auto h = require<float>(item, "h", context);
            auto bg_int = require<int>(item, "bg", context);
            auto x = require<float>(item, "x", context);
            if (!w) {
                return std::unexpected(std::move(w).error());
            }
            if (!h) {
                return std::unexpected(std::move(h).error());
            }
            if (!bg_int) {
                return std::unexpected(std::move(bg_int).error());
            }
            if (!x) {
                return std::unexpected(std::move(x).error());
            }
            if (*bg_int < 0 || *bg_int > static_cast<int>(noted::canvas::PageBackground::dotted)) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument,
                    context + ": bg ordinal " + std::to_string(*bg_int) + " out of range"));
            }
            const auto idx =
                pages.add_page(*w, *h, static_cast<noted::canvas::PageBackground>(*bg_int));
            pages.set_page_origin_x(idx, *x);
        }
        doc.replace_pages(std::move(pages));
    }
    return doc;
}

}  // namespace noted::domain::io
