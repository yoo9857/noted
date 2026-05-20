#include "noted/domain/tool/shape_drag.hpp"

#include <algorithm>
#include <cmath>

namespace noted::domain::tool {

namespace {

[[nodiscard]] auto safe_stroke_width(float v) noexcept -> float {
    // 0.5 px floor — anything below that anti-aliases away to
    // invisible and confuses the user ("I dragged but nothing
    // happened"). NaN / negative collapse to the floor too.
    if (std::isnan(v) || v < 0.5F) {
        return 0.5F;
    }
    return v;
}

}  // namespace

auto bounds_from_drag(double x1, double y1, double x2, double y2) noexcept
    -> std::optional<std::tuple<double, double, double, double>> {
    const double dx = std::abs(x2 - x1);
    const double dy = std::abs(y2 - y1);
    // NaN check via self-comparison — fast and avoids pulling in
    // <cmath>'s std::isnan into the branch hot path.
    if (dx != dx || dy != dy || std::isinf(dx) || std::isinf(dy)) {
        return std::nullopt;
    }
    if (dx < 1.0 || dy < 1.0) {
        return std::nullopt;  // sub-pixel — collapse to nothing
    }
    return std::tuple<double, double, double, double>{
        std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2)};
}

auto shape_from_drag(double x1, double y1, double x2, double y2, const ShapeOptions& opt) noexcept
    -> std::optional<ShapePrimitive> {
    auto bounds = bounds_from_drag(x1, y1, x2, y2);
    if (!bounds.has_value()) {
        return std::nullopt;
    }
    const auto [bx0, by0, bx1, by1] = *bounds;
    ShapePrimitive p{};
    p.kind = opt.kind;
    p.x0 = bx0;
    p.y0 = by0;
    p.x1 = bx1;
    p.y1 = by1;
    p.stroke_r = opt.stroke_r;
    p.stroke_g = opt.stroke_g;
    p.stroke_b = opt.stroke_b;
    p.stroke_a = opt.stroke_a;
    p.stroke_width_px = safe_stroke_width(opt.stroke_width_px);
    return p;
}

}  // namespace noted::domain::tool
