#include "noted/domain/io/document_json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
#include "noted/domain/document/image_asset_registry.hpp"

namespace noted::domain::io {

namespace {

using json = nlohmann::json;

// Top-level keys we recognize. Strict parser rejects any other key.
// `pages` is optional for v1 (back-compat) — its presence is not
// itself an error at any version. `canvas_layers` is v8+; older
// readers reject the key but v8 readers default it to empty when
// absent and migrate v7 strokes into a freshly-created Layer 1.
constexpr std::array<std::string_view, 10> kTopLevelKeys{"version",
                                                         "root",
                                                         "blocks",
                                                         "pages",
                                                         "shapes",
                                                         "texts",
                                                         "images",
                                                         "image_assets",
                                                         "strokes",
                                                         "canvas_layers"};

// Per-pages-object keys.
constexpr std::array<std::string_view, 2> kPagesKeys{"gap_px", "items"};

// Per-page-item keys.
constexpr std::array<std::string_view, 4> kPageItemKeys{"w", "h", "bg", "x"};

// Per-shape-item keys. v3 schema; rectangle + ellipse share the
// same key set (the `k` ordinal disambiguates).
constexpr std::array<std::string_view, 10> kShapeItemKeys{
    "k", "x0", "y0", "x1", "y1", "sw", "r", "g", "b", "a"};

// Per-text-item keys. v4 schema.
constexpr std::array<std::string_view, 8> kTextItemKeys{"x", "y", "s", "fs", "r", "g", "b", "a"};

// Per-image-item keys. v5 schema = first 8; v6 adds "aid". `aid` is
// optional on every version (absent → invalid_asset_id) so v5 files
// load cleanly into v6 readers without a separate key table.
constexpr std::array<std::string_view, 9> kImageItemKeys{
    "x", "y", "w", "h", "r", "g", "b", "a", "aid"};

// Per-image-asset-item keys. v6 schema.
constexpr std::array<std::string_view, 4> kImageAssetItemKeys{"id", "src", "iw", "ih"};

// Per-stroke-item keys. v7 schema = first 3; v8 adds "lid".
// "lid" is optional on every version (absent → invalid_layer_id,
// then migrated on load — see canvas_layers handling).
constexpr std::array<std::string_view, 4> kStrokeItemKeys{"mode", "samples", "style", "lid"};

// Per-canvas-layers-object keys. v8 schema.
constexpr std::array<std::string_view, 2> kCanvasLayersKeys{"active", "items"};

// Per-canvas-layer-item keys. v8 schema base = first 4. `locked` +
// `blend` are additive optional fields; older files load unchanged
// (defaults: locked=false, blend=BlendMode::normal). Writer always
// emits the full set for new files.
constexpr std::array<std::string_view, 6> kCanvasLayerItemKeys{
    "id", "name", "visible", "opacity", "locked", "blend"};

// Per-style-object keys (inside a stroke). v7 schema.
constexpr std::array<std::string_view, 9> kStrokeStyleKeys{
    "min_r", "max_r", "soft", "ag", "r", "g", "b", "a", "stab"};

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

[[nodiscard]] auto serialize_images(const std::vector<noted::domain::tool::ImagePrimitive>& images)
    -> json {
    json arr = json::array();
    for (const auto& im : images) {
        arr.push_back({
            {"x", im.x},
            {"y", im.y},
            {"w", im.width_px},
            {"h", im.height_px},
            {"r", im.r},
            {"g", im.g},
            {"b", im.b},
            {"a", im.a},
            {"aid", im.asset_id},
        });
    }
    return arr;
}

[[nodiscard]] auto serialize_image_assets(const ImageAssetRegistry& registry) -> json {
    json arr = json::array();
    for (const auto& asset : registry.assets()) {
        arr.push_back({
            {"id", asset.id},
            {"src", asset.source_path},
            {"iw", asset.intrinsic_w_px},
            {"ih", asset.intrinsic_h_px},
        });
    }
    return arr;
}

[[nodiscard]] auto serialize_stroke_style(const noted::stroke::BrushStyle& s) -> json {
    return json{
        {"min_r", s.min_radius_px},
        {"max_r", s.max_radius_px},
        {"soft", s.softness_ratio},
        {"ag", s.alpha_gamma},
        {"r", s.r},
        {"g", s.g},
        {"b", s.b},
        {"a", s.a},
        {"stab", s.stabilizer},
    };
}

[[nodiscard]] auto serialize_strokes(const std::vector<noted::stroke::Stroke>& strokes) -> json {
    // Samples are written as a flat float array (x, y, pressure
    // triples) so a 500-sample stroke costs ~1500 numbers instead of
    // 500 small objects with three named fields each. The named-key
    // overhead would be 4-5× larger on disk; the flat layout reads
    // back just as cleanly via a stride-3 loop.
    json arr = json::array();
    for (const auto& stroke : strokes) {
        json samples = json::array();
        samples.get_ptr<json::array_t*>()->reserve(stroke.samples.size() * 3U);
        for (const auto& s : stroke.samples) {
            samples.push_back(s.x);
            samples.push_back(s.y);
            samples.push_back(s.pressure);
        }
        arr.push_back({
            {"mode", static_cast<int>(stroke.mode)},
            {"lid", static_cast<std::uint64_t>(stroke.layer_id)},
            {"samples", std::move(samples)},
            {"style", serialize_stroke_style(stroke.style)},
        });
    }
    return arr;
}

[[nodiscard]] auto serialize_canvas_layers(const CanvasLayerStack& stack,
                                           noted::LayerId active) -> json {
    json items = json::array();
    for (const auto& layer : stack.layers()) {
        items.push_back({
            {"id", static_cast<std::uint64_t>(layer.id)},
            {"name", layer.name},
            {"visible", layer.visible},
            {"opacity", layer.opacity},
            {"locked", layer.locked},
            {"blend", static_cast<int>(layer.blend)},
        });
    }
    return json{
        {"active", static_cast<std::uint64_t>(active)},
        {"items", std::move(items)},
    };
}

[[nodiscard]] auto serialize_texts(const std::vector<noted::domain::tool::TextPrimitive>& texts)
    -> json {
    json arr = json::array();
    for (const auto& t : texts) {
        arr.push_back({
            {"x", t.x},
            {"y", t.y},
            {"s", t.content},
            {"fs", t.font_size_px},
            {"r", t.r},
            {"g", t.g},
            {"b", t.b},
            {"a", t.a},
        });
    }
    return arr;
}

[[nodiscard]] auto serialize_shapes(const std::vector<noted::domain::tool::ShapePrimitive>& shapes)
    -> json {
    json arr = json::array();
    for (const auto& s : shapes) {
        arr.push_back({
            {"k", static_cast<int>(s.kind)},
            {"x0", s.x0},
            {"y0", s.y0},
            {"x1", s.x1},
            {"y1", s.y1},
            {"sw", s.stroke_width_px},
            {"r", s.stroke_r},
            {"g", s.stroke_g},
            {"b", s.stroke_b},
            {"a", s.stroke_a},
        });
    }
    return arr;
}

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
    out["shapes"] = serialize_shapes(doc.shapes());
    out["texts"] = serialize_texts(doc.texts());
    out["images"] = serialize_images(doc.images());
    out["image_assets"] = serialize_image_assets(doc.image_assets());
    out["strokes"] = serialize_strokes(doc.strokes());
    out["canvas_layers"] = serialize_canvas_layers(doc.canvas_layers(), doc.active_layer());

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

    // Shapes — optional at every version. v1/v2 files have no
    // `shapes` field; loader treats absent as empty.
    if (root_obj.contains("shapes")) {
        const auto& shapes_arr = root_obj.at("shapes");
        if (!shapes_arr.is_array()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'shapes' is not an array"));
        }
        std::vector<noted::domain::tool::ShapePrimitive> shapes;
        shapes.reserve(shapes_arr.size());
        for (std::size_t i = 0; i < shapes_arr.size(); ++i) {
            const std::string context = "shapes[" + std::to_string(i) + "]";
            const auto& item = shapes_arr[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kShapeItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }
            auto k = require<int>(item, "k", context);
            auto x0 = require<double>(item, "x0", context);
            auto y0 = require<double>(item, "y0", context);
            auto x1 = require<double>(item, "x1", context);
            auto y1 = require<double>(item, "y1", context);
            auto sw = require<float>(item, "sw", context);
            auto r = require<float>(item, "r", context);
            auto g = require<float>(item, "g", context);
            auto b = require<float>(item, "b", context);
            auto a = require<float>(item, "a", context);
            if (!k) {
                return std::unexpected(std::move(k).error());
            }
            if (!x0) {
                return std::unexpected(std::move(x0).error());
            }
            if (!y0) {
                return std::unexpected(std::move(y0).error());
            }
            if (!x1) {
                return std::unexpected(std::move(x1).error());
            }
            if (!y1) {
                return std::unexpected(std::move(y1).error());
            }
            if (!sw) {
                return std::unexpected(std::move(sw).error());
            }
            if (!r) {
                return std::unexpected(std::move(r).error());
            }
            if (!g) {
                return std::unexpected(std::move(g).error());
            }
            if (!b) {
                return std::unexpected(std::move(b).error());
            }
            if (!a) {
                return std::unexpected(std::move(a).error());
            }
            if (*k < 0 || *k > static_cast<int>(noted::domain::tool::ShapeKind::ellipse)) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument,
                    context + ": k ordinal " + std::to_string(*k) + " out of range"));
            }
            noted::domain::tool::ShapePrimitive s{};
            s.kind = static_cast<noted::domain::tool::ShapeKind>(*k);
            s.x0 = *x0;
            s.y0 = *y0;
            s.x1 = *x1;
            s.y1 = *y1;
            s.stroke_width_px = *sw;
            s.stroke_r = *r;
            s.stroke_g = *g;
            s.stroke_b = *b;
            s.stroke_a = *a;
            shapes.push_back(s);
        }
        doc.replace_shapes(std::move(shapes));
    }

    // Texts — optional at every version. v1/v2/v3 files have no
    // `texts` field; loader treats absent as empty.
    if (root_obj.contains("texts")) {
        const auto& texts_arr = root_obj.at("texts");
        if (!texts_arr.is_array()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument, "document_from_json: 'texts' is not an array"));
        }
        std::vector<noted::domain::tool::TextPrimitive> texts;
        texts.reserve(texts_arr.size());
        for (std::size_t i = 0; i < texts_arr.size(); ++i) {
            const std::string context = "texts[" + std::to_string(i) + "]";
            const auto& item = texts_arr[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kTextItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }
            auto x = require<double>(item, "x", context);
            auto y = require<double>(item, "y", context);
            auto s = require<std::string>(item, "s", context);
            auto fs = require<float>(item, "fs", context);
            auto r = require<float>(item, "r", context);
            auto g = require<float>(item, "g", context);
            auto b = require<float>(item, "b", context);
            auto a = require<float>(item, "a", context);
            if (!x) {
                return std::unexpected(std::move(x).error());
            }
            if (!y) {
                return std::unexpected(std::move(y).error());
            }
            if (!s) {
                return std::unexpected(std::move(s).error());
            }
            if (!fs) {
                return std::unexpected(std::move(fs).error());
            }
            if (!r) {
                return std::unexpected(std::move(r).error());
            }
            if (!g) {
                return std::unexpected(std::move(g).error());
            }
            if (!b) {
                return std::unexpected(std::move(b).error());
            }
            if (!a) {
                return std::unexpected(std::move(a).error());
            }
            // Clamp font size at load time so a corrupted / hostile
            // file (e.g. `"fs": -3` or `"fs": NaN`) cannot smuggle a
            // degenerate primitive past the loader. Matches the 1 px
            // floor `text_primitive_from` applies on the commit path.
            const float clamped_fs = (std::isnan(*fs) || *fs < 1.0F) ? 1.0F : *fs;
            noted::domain::tool::TextPrimitive t{};
            t.x = *x;
            t.y = *y;
            t.content = std::move(*s);
            t.font_size_px = clamped_fs;
            t.r = *r;
            t.g = *g;
            t.b = *b;
            t.a = *a;
            texts.push_back(std::move(t));
        }
        doc.replace_texts(std::move(texts));
    }

    // Images — optional at every version. v1..v4 have no `images`
    // field; loader treats absent as empty.
    if (root_obj.contains("images")) {
        const auto& images_arr = root_obj.at("images");
        if (!images_arr.is_array()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'images' is not an array"));
        }
        std::vector<noted::domain::tool::ImagePrimitive> images;
        images.reserve(images_arr.size());
        for (std::size_t i = 0; i < images_arr.size(); ++i) {
            const std::string context = "images[" + std::to_string(i) + "]";
            const auto& item = images_arr[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kImageItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }
            auto x = require<double>(item, "x", context);
            auto y = require<double>(item, "y", context);
            auto w = require<float>(item, "w", context);
            auto h = require<float>(item, "h", context);
            auto r = require<float>(item, "r", context);
            auto g = require<float>(item, "g", context);
            auto b = require<float>(item, "b", context);
            auto a = require<float>(item, "a", context);
            if (!x) {
                return std::unexpected(std::move(x).error());
            }
            if (!y) {
                return std::unexpected(std::move(y).error());
            }
            if (!w) {
                return std::unexpected(std::move(w).error());
            }
            if (!h) {
                return std::unexpected(std::move(h).error());
            }
            if (!r) {
                return std::unexpected(std::move(r).error());
            }
            if (!g) {
                return std::unexpected(std::move(g).error());
            }
            if (!b) {
                return std::unexpected(std::move(b).error());
            }
            if (!a) {
                return std::unexpected(std::move(a).error());
            }
            // Clamp dimensions to a 1-px floor — matches the runtime
            // clamp `image_primitive_from` applies on the commit path.
            // Hostile / corrupted files cannot smuggle a zero-area
            // primitive past the loader.
            const float clamped_w = (std::isnan(*w) || *w < 1.0F) ? 1.0F : *w;
            const float clamped_h = (std::isnan(*h) || *h < 1.0F) ? 1.0F : *h;
            noted::domain::tool::ImagePrimitive im{};
            im.x = *x;
            im.y = *y;
            im.width_px = clamped_w;
            im.height_px = clamped_h;
            im.r = *r;
            im.g = *g;
            im.b = *b;
            im.a = *a;
            // v6+: optional `aid` field. Absent (v5 files) ⇒
            // `invalid_asset_id`, which is the placeholder sentinel.
            if (item.contains("aid")) {
                auto aid = require<noted::domain::AssetId>(item, "aid", context);
                if (!aid) {
                    return std::unexpected(std::move(aid).error());
                }
                im.asset_id = *aid;
            }
            images.push_back(im);
        }
        doc.replace_images(std::move(images));
    }

    // Image assets — v6+. Optional at every version; absent ⇒ empty
    // registry. After the registry is built, enforce referential
    // integrity: every non-zero `asset_id` on an `ImagePrimitive`
    // must resolve to an asset in the registry. v5 files never set
    // a non-zero `aid`, so they pass trivially.
    if (root_obj.contains("image_assets")) {
        const auto& assets_arr = root_obj.at("image_assets");
        if (!assets_arr.is_array()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'image_assets' is not an array"));
        }
        ImageAssetRegistry registry;
        for (std::size_t i = 0; i < assets_arr.size(); ++i) {
            const std::string context = "image_assets[" + std::to_string(i) + "]";
            const auto& item = assets_arr[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kImageAssetItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }
            auto id = require<noted::domain::AssetId>(item, "id", context);
            auto src = require<std::string>(item, "src", context);
            auto iw = require<std::uint32_t>(item, "iw", context);
            auto ih = require<std::uint32_t>(item, "ih", context);
            if (!id) {
                return std::unexpected(std::move(id).error());
            }
            if (!src) {
                return std::unexpected(std::move(src).error());
            }
            if (!iw) {
                return std::unexpected(std::move(iw).error());
            }
            if (!ih) {
                return std::unexpected(std::move(ih).error());
            }
            ImageAsset asset{};
            asset.id = *id;
            asset.source_path = std::move(*src);
            asset.intrinsic_w_px = *iw;
            asset.intrinsic_h_px = *ih;
            auto inserted = registry.insert(std::move(asset));
            if (!inserted) {
                // Re-wrap the registry-side error with the loader's
                // context so the user sees which array index was
                // duplicated / invalid.
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument, context + ": " + inserted.error().message));
            }
        }
        doc.replace_image_assets(std::move(registry));
    }

    // Referential integrity — runs regardless of whether
    // `image_assets` was present (a v5 file with no registry and
    // implicit aid=0 everywhere still has to satisfy this check).
    for (std::size_t i = 0; i < doc.images().size(); ++i) {
        const auto& im = doc.images()[i];
        if (im.asset_id == noted::domain::invalid_asset_id) {
            continue;  // placeholder — no registry lookup required
        }
        if (doc.image_assets().find(im.asset_id) == nullptr) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                "document_from_json: images[" + std::to_string(i) + "].aid " +
                    std::to_string(im.asset_id) + " does not resolve in image_assets"));
        }
    }

    // Strokes — v7+. Optional at every version. v1..v6 files load
    // with an empty strokes list. Sample arrays use the flat (x, y,
    // pressure) triple layout — see the schema doc.
    if (root_obj.contains("strokes")) {
        const auto& strokes_arr = root_obj.at("strokes");
        if (!strokes_arr.is_array()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'strokes' is not an array"));
        }
        std::vector<noted::stroke::Stroke> strokes;
        strokes.reserve(strokes_arr.size());
        for (std::size_t i = 0; i < strokes_arr.size(); ++i) {
            const std::string context = "strokes[" + std::to_string(i) + "]";
            const auto& item = strokes_arr[i];
            if (!item.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": not an object"));
            }
            if (auto bad = find_unknown_key(item, kStrokeItemKeys); !bad.empty()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": unknown key '" + bad + "'"));
            }

            auto mode_int = require<int>(item, "mode", context);
            if (!mode_int) {
                return std::unexpected(std::move(mode_int).error());
            }
            if (*mode_int < 0 || *mode_int > static_cast<int>(noted::stroke::DrawMode::erase)) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument,
                    context + ": mode ordinal " + std::to_string(*mode_int) + " out of range"));
            }

            // Samples — flat float array, length must be a multiple of 3.
            if (!item.contains("samples")) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": missing 'samples'"));
            }
            const auto& samples_arr = item.at("samples");
            if (!samples_arr.is_array()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": 'samples' is not an array"));
            }
            if (samples_arr.size() % 3U != 0U) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument,
                    context + ": 'samples' length " + std::to_string(samples_arr.size()) +
                        " is not a multiple of 3 (x, y, pressure triples)"));
            }
            std::vector<noted::stroke::StrokeSample> samples;
            samples.reserve(samples_arr.size() / 3U);
            for (std::size_t j = 0; j + 2U < samples_arr.size(); j += 3U) {
                noted::stroke::StrokeSample s{};
                try {
                    s.x = samples_arr[j].get<float>();
                    s.y = samples_arr[j + 1U].get<float>();
                    s.pressure = samples_arr[j + 2U].get<float>();
                } catch (const json::exception& e) {
                    return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                             context + ": 'samples'[" +
                                                                 std::to_string(j) +
                                                                 "..] non-numeric — " + e.what()));
                }
                samples.push_back(s);
            }

            // Style block.
            if (!item.contains("style")) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": missing 'style'"));
            }
            const auto& style_obj = item.at("style");
            if (!style_obj.is_object()) {
                return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                         context + ": 'style' is not an object"));
            }
            if (auto bad = find_unknown_key(style_obj, kStrokeStyleKeys); !bad.empty()) {
                return std::unexpected(
                    noted::make_error(noted::ErrorCode::invalid_argument,
                                      context + ".style: unknown key '" + bad + "'"));
            }
            auto min_r = require<float>(style_obj, "min_r", context + ".style");
            auto max_r = require<float>(style_obj, "max_r", context + ".style");
            auto soft = require<float>(style_obj, "soft", context + ".style");
            auto ag = require<float>(style_obj, "ag", context + ".style");
            auto sr = require<float>(style_obj, "r", context + ".style");
            auto sg = require<float>(style_obj, "g", context + ".style");
            auto sb = require<float>(style_obj, "b", context + ".style");
            auto sa = require<float>(style_obj, "a", context + ".style");
            auto stab = require<float>(style_obj, "stab", context + ".style");
            if (!min_r) {
                return std::unexpected(std::move(min_r).error());
            }
            if (!max_r) {
                return std::unexpected(std::move(max_r).error());
            }
            if (!soft) {
                return std::unexpected(std::move(soft).error());
            }
            if (!ag) {
                return std::unexpected(std::move(ag).error());
            }
            if (!sr) {
                return std::unexpected(std::move(sr).error());
            }
            if (!sg) {
                return std::unexpected(std::move(sg).error());
            }
            if (!sb) {
                return std::unexpected(std::move(sb).error());
            }
            if (!sa) {
                return std::unexpected(std::move(sa).error());
            }
            if (!stab) {
                return std::unexpected(std::move(stab).error());
            }
            // Clamp degenerate radii / gamma at the data boundary so
            // a corrupted file can't smuggle a zero / negative width
            // past the renderer's vertex math.
            noted::stroke::BrushStyle style{};
            style.min_radius_px = (std::isnan(*min_r) || *min_r < 0.5F) ? 0.5F : *min_r;
            style.max_radius_px =
                (std::isnan(*max_r) || *max_r < style.min_radius_px) ? style.min_radius_px : *max_r;
            style.softness_ratio = std::isnan(*soft) ? 0.0F : *soft;
            style.alpha_gamma = (std::isnan(*ag) || *ag <= 0.0F) ? 1.0F : *ag;
            style.r = *sr;
            style.g = *sg;
            style.b = *sb;
            style.a = *sa;
            style.stabilizer = (std::isnan(*stab) || *stab < 0.0F) ? 0.0F
                               : (*stab > 0.95F)                   ? 0.95F
                                                                   : *stab;

            // `lid` is v8+ but optional on every version. Absent →
            // invalid_layer_id (= 0); the migration block below either
            // (a) creates a Layer 1 and rewrites it for v7 strokes, or
            // (b) leaves it unassigned so the renderer treats it as
            // hidden (clean failure mode for hand-crafted files).
            noted::LayerId lid = noted::invalid_layer_id;
            if (item.contains("lid")) {
                try {
                    lid = item.at("lid").get<noted::LayerId>();
                } catch (const json::exception& e) {
                    return std::unexpected(
                        noted::make_error(noted::ErrorCode::invalid_argument,
                                          context + ": 'lid' non-integral — " + e.what()));
                }
            }

            noted::stroke::Stroke stroke{};
            stroke.samples = std::move(samples);
            stroke.style = style;
            stroke.mode = static_cast<noted::stroke::DrawMode>(*mode_int);
            stroke.layer_id = lid;
            strokes.push_back(std::move(stroke));
        }
        doc.replace_strokes(std::move(strokes));
    }

    // Canvas layers — v8+. Optional at every version. v7 files with
    // any strokes get a default "Layer 1" lazily materialised below
    // and every stroke is pinned to its id, keeping v7 round-trip
    // semantics intact for v8 readers.
    CanvasLayerStack layer_stack;
    noted::LayerId active_layer = noted::invalid_layer_id;

    if (root_obj.contains("canvas_layers")) {
        const auto& cl_obj = root_obj.at("canvas_layers");
        if (!cl_obj.is_object()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  "document_from_json: 'canvas_layers' is not an object"));
        }
        if (auto bad = find_unknown_key(cl_obj, kCanvasLayersKeys); !bad.empty()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_argument,
                std::string{"document_from_json: 'canvas_layers' unknown key '"} + bad + "'"));
        }

        if (cl_obj.contains("active")) {
            try {
                active_layer = cl_obj.at("active").get<noted::LayerId>();
            } catch (const json::exception& e) {
                return std::unexpected(noted::make_error(
                    noted::ErrorCode::invalid_argument,
                    std::string{"document_from_json: 'canvas_layers.active' non-integral — "} +
                        e.what()));
            }
        }

        std::vector<CanvasLayer> items;
        if (cl_obj.contains("items")) {
            const auto& items_arr = cl_obj.at("items");
            if (!items_arr.is_array()) {
                return std::unexpected(
                    noted::make_error(noted::ErrorCode::invalid_argument,
                                      "document_from_json: 'canvas_layers.items' is not an array"));
            }
            items.reserve(items_arr.size());
            std::unordered_set<noted::LayerId> seen_ids;
            for (std::size_t i = 0; i < items_arr.size(); ++i) {
                const std::string ctx = "canvas_layers.items[" + std::to_string(i) + "]";
                const auto& it = items_arr[i];
                if (!it.is_object()) {
                    return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                             ctx + ": not an object"));
                }
                if (auto bad = find_unknown_key(it, kCanvasLayerItemKeys); !bad.empty()) {
                    return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                             ctx + ": unknown key '" + bad + "'"));
                }
                auto id_v = require<noted::LayerId>(it, "id", ctx);
                if (!id_v) {
                    return std::unexpected(std::move(id_v).error());
                }
                if (*id_v == noted::invalid_layer_id) {
                    return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                             ctx + ": id 0 is reserved"));
                }
                if (!seen_ids.insert(*id_v).second) {
                    return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                             ctx + ": duplicate id"));
                }
                auto name_v = require<std::string>(it, "name", ctx);
                auto visible_v = require<bool>(it, "visible", ctx);
                auto opacity_v = require<float>(it, "opacity", ctx);
                if (!name_v) {
                    return std::unexpected(std::move(name_v).error());
                }
                if (!visible_v) {
                    return std::unexpected(std::move(visible_v).error());
                }
                if (!opacity_v) {
                    return std::unexpected(std::move(opacity_v).error());
                }
                CanvasLayer layer{};
                layer.id = *id_v;
                layer.name = std::move(*name_v);
                layer.visible = *visible_v;
                layer.opacity = *opacity_v;  // CanvasLayerStack::replace clamps to [0,1]
                // Optional additive fields — older v8 files predate
                // these and load with defaults (unlocked, normal
                // blend), staying forward-compatible.
                if (it.contains("locked")) {
                    try {
                        layer.locked = it.at("locked").get<bool>();
                    } catch (const json::exception& e) {
                        return std::unexpected(
                            noted::make_error(noted::ErrorCode::invalid_argument,
                                              ctx + ": 'locked' non-boolean — " + e.what()));
                    }
                }
                if (it.contains("blend")) {
                    int blend_ord = 0;
                    try {
                        blend_ord = it.at("blend").get<int>();
                    } catch (const json::exception& e) {
                        return std::unexpected(
                            noted::make_error(noted::ErrorCode::invalid_argument,
                                              ctx + ": 'blend' non-integral — " + e.what()));
                    }
                    if (blend_ord < 0 ||
                        blend_ord > static_cast<int>(noted::domain::BlendMode::luminosity)) {
                        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                                 ctx + ": 'blend' ordinal " +
                                                                     std::to_string(blend_ord) +
                                                                     " out of range"));
                    }
                    layer.blend = static_cast<noted::domain::BlendMode>(blend_ord);
                }
                items.push_back(std::move(layer));
            }
        }
        layer_stack.replace(std::move(items));
    }

    // v7 → v8 migration: if the file has strokes but no canvas_layers
    // block (legacy v7 or hand-written v8 omitting layers), spin up a
    // default "Layer 1" and pin every stroke to it. Idempotent for v8
    // files that already serialized a stack — they enter this branch
    // with the stack already populated and skip the synth step.
    if (!doc.strokes().empty() && layer_stack.empty()) {
        auto new_id = layer_stack.add_layer("Layer 1");
        if (!new_id) {
            return std::unexpected(std::move(new_id).error());
        }
        active_layer = *new_id;
        // Rewrite stroke layer_ids in place. replace_strokes is the
        // loader bypass; doing it again here is cheap (one pass) and
        // keeps the stamp policy uniform regardless of input version.
        auto migrated = doc.strokes();
        for (auto& s : migrated) {
            s.layer_id = *new_id;
        }
        doc.replace_strokes(std::move(migrated));
    } else if (layer_stack.empty()) {
        // No strokes and no stack → leave both empty; the next user
        // action (a stroke landing on the canvas) lazy-creates Layer 1
        // via Document::add_stroke's auto-stamp path.
    }

    doc.replace_canvas_layers(std::move(layer_stack), active_layer);

    return doc;
}

}  // namespace noted::domain::io
