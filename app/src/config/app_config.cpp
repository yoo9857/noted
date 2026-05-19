#include "config/app_config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "noted/platform/fs/fs.hpp"

namespace noted::app::config {

namespace {

constexpr const char* kConfigFileName = "noted.config.json";

// Per-field env var lookups — opt-in overrides for dev workflows
// without touching the config file.
[[nodiscard]] auto env_string(const char* name) -> std::string {
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return {};
    }
    return std::string{v};
}

// Read an optional sub-object's key if present. Defaulted output
// stays untouched on missing keys — preserves `defaults()` values.
template <typename T>
void read_opt(const nlohmann::json& j, const char* key, T& out) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        try {
            out = it->template get<T>();
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[config] ignoring malformed key '" << key << "': " << e.what() << '\n';
        }
    }
}

void read_theme(const nlohmann::json& j, const char* key, noted::ui::theme::ThemeKind& out) {
    if (auto it = j.find(key); it != j.end() && it->is_string()) {
        const auto s = it->template get<std::string>();
        if (s == "dark") {
            out = noted::ui::theme::ThemeKind::dark;
        } else if (s == "light") {
            out = noted::ui::theme::ThemeKind::light;
        } else {
            std::cerr << "[config] unknown theme '" << s << "' — falling back to default\n";
        }
    }
}

void read_path(const nlohmann::json& j, const char* key, std::filesystem::path& out) {
    if (auto it = j.find(key); it != j.end() && it->is_string()) {
        out = std::filesystem::path{it->template get<std::string>()};
    }
}

}  // namespace

auto AppConfig::load_from_file(const std::filesystem::path& path) -> Result<AppConfig> {
    std::ifstream in(path);
    if (!in.is_open()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::file_not_found,
                              "AppConfig::load_from_file: cannot open " + path.string()));
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto text = ss.str();

    AppConfig cfg = AppConfig::defaults();
    try {
        const auto root = nlohmann::json::parse(text,
                                                /*cb*/ nullptr,
                                                /*allow_exceptions*/ true,
                                                /*ignore_comments*/ true);
        if (!root.is_object()) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state, "AppConfig: top-level JSON must be an object"));
        }

        if (auto it = root.find("window"); it != root.end() && it->is_object()) {
            read_opt(*it, "width", cfg.window.width);
            read_opt(*it, "height", cfg.window.height);
            read_opt(*it, "title", cfg.window.title);
        }
        if (auto it = root.find("canvas"); it != root.end() && it->is_object()) {
            read_opt(*it, "framesInFlight", cfg.canvas.frames_in_flight);
            read_opt(*it, "zoomStep", cfg.canvas.zoom_step);
            read_opt(*it, "zoomMin", cfg.canvas.zoom_min);
            read_opt(*it, "zoomMax", cfg.canvas.zoom_max);
        }
        if (auto it = root.find("font"); it != root.end() && it->is_object()) {
            read_path(*it, "cjkPath", cfg.font.cjk_font_path);
            read_opt(*it, "sizePx", cfg.font.size_px);
        }
        if (auto it = root.find("assets"); it != root.end() && it->is_object()) {
            read_path(*it, "shaderDir", cfg.assets.shader_dir);
        }
        if (auto it = root.find("ui"); it != root.end() && it->is_object()) {
            read_theme(*it, "theme", cfg.ui.default_theme);
        }
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              std::string{"AppConfig: JSON parse failed — "} + e.what()));
    }

    return cfg;
}

auto resolve_shader_dir(const AppConfig& cfg) -> std::filesystem::path {
    std::error_code ec;
    const auto check = [&](const std::filesystem::path& candidate) -> std::filesystem::path {
        if (candidate.empty()) {
            return {};
        }
        if (std::filesystem::is_directory(candidate, ec) && !ec) {
            return candidate;
        }
        return {};
    };

    // 1. Explicit config override.
    if (auto p = check(cfg.assets.shader_dir); !p.empty()) {
        return p;
    }
    // 2. Env var (dev workflow — point at the build's shader output
    //    without rebuilding the config file).
    if (auto p = check(std::filesystem::path{env_string("NOTED_SHADER_DIR")}); !p.empty()) {
        return p;
    }
    // 3. Production layout: shaders/ next to the .exe.
    const auto exe = noted::platform::fs::executable_dir();
    if (!exe.empty()) {
        if (auto p = check(exe / "shaders"); !p.empty()) {
            return p;
        }
    }
    // 4. Compile-time fallback (NOTED_SHADER_DIR define from the
    //    CMake build). Lets the dev workflow `cmake --build && run`
    //    continue working without any explicit setup.
#ifdef NOTED_SHADER_DIR
    if (auto p = check(std::filesystem::path{NOTED_SHADER_DIR}); !p.empty()) {
        return p;
    }
#endif
    return {};
}

auto load_default() -> AppConfig {
    // Where to look for the config file: only the standard slot
    // next to the executable, for the moment. A future "user
    // settings dir" search (AppData on Windows, ~/Library on
    // macOS, ~/.config on Linux) is a clean follow-up.
    const auto exe = noted::platform::fs::executable_dir();
    if (exe.empty()) {
        return AppConfig::defaults();
    }
    const auto path = exe / kConfigFileName;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return AppConfig::defaults();
    }
    auto cfg = AppConfig::load_from_file(path);
    if (!cfg) {
        std::cerr << "[config] " << cfg.error().format() << " — falling back to defaults\n";
        return AppConfig::defaults();
    }
    return std::move(*cfg);
}

}  // namespace noted::app::config
