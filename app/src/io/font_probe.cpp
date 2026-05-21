#include "io/font_probe.hpp"

#include <array>
#include <system_error>

namespace noted::app {

auto probe_cjk_font() -> std::filesystem::path {
    static const std::array<const char*, 6> kCandidates{
#if defined(_WIN32)
        // Malgun Gothic ships with every Windows since Vista. The
        // bold variant is the secondary fallback for the rare slim
        // image where the regular face was uninstalled.
        "C:/Windows/Fonts/malgun.ttf",
        "C:/Windows/Fonts/malgunbd.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/Library/Fonts/AppleGothic.ttf",
#else
        // Debian/Ubuntu via fonts-noto-cjk; Arch via noto-fonts-cjk.
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
#endif
        nullptr,
        nullptr,
    };
    std::error_code ec;
    for (const auto* p : kCandidates) {
        if (p == nullptr) {
            continue;
        }
        std::filesystem::path candidate{p};
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

auto probe_symbol_font() -> std::filesystem::path {
    // Symbol-rich fallback font merged into the ImGui atlas after the
    // primary CJK face. Used for UI icon glyphs (✎ ⌫ ▭ ▢ ▦ ↺ ↻ etc.)
    // that the CJK face doesn't carry — Malgun Gothic has Hangul +
    // Latin but is sparse on Misc Symbols / Dingbats. Without this
    // fallback the toolbar buttons render as the "missing glyph"
    // tofu box.
    //
    // Windows: Segoe UI Symbol (seguisym) carries the full BMP
    //   symbol range; Segoe MDL2 (segmdl2) is the secondary
    //   fallback for the rare image where seguisym isn't installed.
    // macOS: Apple Symbols provides the same coverage.
    // Linux: DejaVu Sans has wide BMP coverage as a generic fallback.
    static const std::array<const char*, 6> kCandidates{
#if defined(_WIN32)
        "C:/Windows/Fonts/seguisym.ttf",
        "C:/Windows/Fonts/segmdl2.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/Library/Fonts/Symbol.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
#endif
        nullptr,
        nullptr,
    };
    std::error_code ec;
    for (const auto* p : kCandidates) {
        if (p == nullptr) {
            continue;
        }
        std::filesystem::path candidate{p};
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

}  // namespace noted::app
