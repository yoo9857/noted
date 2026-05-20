#include "noted/domain/tool/text_input.hpp"

#include <cmath>
#include <utility>

namespace noted::domain::tool {

namespace {

[[nodiscard]] auto safe_font_size(float v) noexcept -> float {
    if (std::isnan(v) || v < 1.0F) {
        return 1.0F;
    }
    return v;
}

// Strip leading + trailing ASCII whitespace. Unicode whitespace
// (NBSP, IDEOGRAPHIC SPACE) is preserved on purpose — a CJK user
// who typed a fullwidth space probably meant it.
void trim_ascii_(std::string& s) noexcept {
    while (!s.empty() &&
           (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) {
        s.erase(s.begin());
    }
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
}

}  // namespace

auto text_primitive_from(double x,
                         double y,
                         std::string content,
                         const TextOptions& opt) noexcept -> TextPrimitive {
    trim_ascii_(content);
    TextPrimitive p{};
    p.x = x;
    p.y = y;
    p.content = std::move(content);
    p.font_size_px = safe_font_size(opt.font_size_px);
    p.r = opt.r;
    p.g = opt.g;
    p.b = opt.b;
    p.a = opt.a;
    return p;
}

}  // namespace noted::domain::tool
