#include "noted/domain/io/brush_library_json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "noted/domain/tool/brush_library.hpp"
#include "noted/domain/tool/brush_preset.hpp"

namespace noted::domain::io {

namespace {

using json = nlohmann::json;

constexpr std::array<std::string_view, 2> kTopLevelKeys{"version", "presets"};

constexpr std::array<std::string_view, 16> kPresetKeys{"id",
                                                       "name",
                                                       "kind",
                                                       "min_r",
                                                       "max_r",
                                                       "ag",
                                                       "r",
                                                       "g",
                                                       "b",
                                                       "a",
                                                       "stab",
                                                       "soft",
                                                       "spacing",
                                                       "scatter",
                                                       "angle",
                                                       "ajitter"};

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

template <std::size_t N>
[[nodiscard]] auto find_unknown_key(const json& obj,
                                    const std::array<std::string_view, N>& allowed) -> std::string {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!contains(allowed, it.key())) {
            return it.key();
        }
    }
    return {};
}

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

[[nodiscard]] auto serialize_preset(const noted::domain::tool::BrushPreset& p) -> json {
    return json{
        {"id", static_cast<std::uint64_t>(p.id)},
        {"name", p.name},
        {"kind", static_cast<int>(p.kind)},
        {"min_r", p.min_radius_px},
        {"max_r", p.max_radius_px},
        {"ag", p.alpha_gamma},
        {"r", p.r},
        {"g", p.g},
        {"b", p.b},
        {"a", p.a},
        {"stab", p.stabilizer},
        {"soft", p.softness},
        {"spacing", p.spacing},
        {"scatter", p.scatter},
        {"angle", p.angle_deg},
        {"ajitter", p.angle_jitter},
    };
}

}  // namespace

auto brush_library_user_to_json(const noted::domain::tool::BrushLibrary& lib) -> std::string {
    json out;
    out["version"] = kBrushLibraryJsonVersion;
    json presets = json::array();
    for (const auto& p : lib.presets()) {
        if (lib.is_builtin(p.id)) {
            continue;  // factory presets are seeded on launch, not saved
        }
        presets.push_back(serialize_preset(p));
    }
    out["presets"] = std::move(presets);
    return out.dump(2);
}

auto brush_library_user_from_json(std::string_view json_text,
                                  noted::domain::tool::BrushLibrary& lib) -> Result<void> {
    json root_obj;
    try {
        root_obj = json::parse(json_text);
    } catch (const json::parse_error& e) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            std::string{"brush_library_user_from_json: malformed JSON — "} + e.what()));
    }
    if (!root_obj.is_object()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "brush_library_user_from_json: root is not an object"));
    }
    if (auto bad = find_unknown_key(root_obj, kTopLevelKeys); !bad.empty()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            std::string{"brush_library_user_from_json: unknown top-level key '"} + bad + "'"));
    }
    auto version = require<int>(root_obj, "version", "brush_library_user_from_json");
    if (!version) {
        return std::unexpected(std::move(version).error());
    }
    if (*version < 1 || *version > kBrushLibraryJsonVersion) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "brush_library_user_from_json: unsupported version " + std::to_string(*version)));
    }

    if (!root_obj.contains("presets")) {
        return {};  // empty user library is valid
    }
    const auto& arr = root_obj.at("presets");
    if (!arr.is_array()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "brush_library_user_from_json: 'presets' is not an array"));
    }

    // Validate-then-mutate — parse every preset into a staging
    // vector first so a malformed entry doesn't leave the library
    // half-populated.
    std::vector<noted::domain::tool::BrushPreset> staging;
    staging.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string ctx = "presets[" + std::to_string(i) + "]";
        const auto& it = arr[i];
        if (!it.is_object()) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument, ctx + ": not an object"));
        }
        if (auto bad = find_unknown_key(it, kPresetKeys); !bad.empty()) {
            return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                     ctx + ": unknown key '" + bad + "'"));
        }
        noted::domain::tool::BrushPreset p{};
        auto id_v = require<std::uint64_t>(it, "id", ctx);
        if (!id_v) {
            return std::unexpected(std::move(id_v).error());
        }
        if (*id_v == 0) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument, ctx + ": id 0 is reserved"));
        }
        p.id = *id_v;
        auto name_v = require<std::string>(it, "name", ctx);
        if (!name_v) {
            return std::unexpected(std::move(name_v).error());
        }
        p.name = std::move(*name_v);

        auto kind_v = require<int>(it, "kind", ctx);
        if (!kind_v) {
            return std::unexpected(std::move(kind_v).error());
        }
        if (*kind_v < 0 || *kind_v > static_cast<int>(noted::domain::tool::BrushKind::texture)) {
            return std::unexpected(
                noted::make_error(noted::ErrorCode::invalid_argument,
                                  ctx + ": kind " + std::to_string(*kind_v) + " out of range"));
        }
        p.kind = static_cast<noted::domain::tool::BrushKind>(*kind_v);

        auto get_float = [&](std::string_view key, float& out_val) -> Result<void> {
            auto r = require<float>(it, key, ctx);
            if (!r) {
                return std::unexpected(std::move(r).error());
            }
            out_val = *r;
            return {};
        };
        if (auto r = get_float("min_r", p.min_radius_px); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("max_r", p.max_radius_px); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("ag", p.alpha_gamma); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("r", p.r); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("g", p.g); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("b", p.b); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("a", p.a); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("stab", p.stabilizer); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("soft", p.softness); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("spacing", p.spacing); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("scatter", p.scatter); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("angle", p.angle_deg); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = get_float("ajitter", p.angle_jitter); !r) {
            return std::unexpected(std::move(r).error());
        }
        // `use_preset_color` not persisted in v1 — user-added presets
        // default to honouring the saved color so a re-load reproduces
        // the user's intended visual. Future schema rev can add it.
        p.use_preset_color = true;
        staging.push_back(std::move(p));
    }

    // Commit: insert each preset (which detects duplicate ids and
    // bumps the allocator). If ANY insert fails we already mutated
    // the library — but in practice the only failure mode is "id
    // already present", which can happen if a user's saved file
    // collides with a factory id. Skip such entries with a stderr
    // warning rather than failing the whole load.
    for (auto& p : staging) {
        if (auto r = lib.insert(std::move(p)); !r) {
            // Skip and continue — losing one user preset due to a
            // future-factory-id collision is better than dropping
            // the whole save file.
        }
    }
    return {};
}

}  // namespace noted::domain::io
