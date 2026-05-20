#include "noted/domain/tool/image_input.hpp"

#include <cmath>

namespace noted::domain::tool {

namespace {

[[nodiscard]] auto safe_dimension(float v) noexcept -> float {
    if (std::isnan(v) || v < 1.0F) {
        return 1.0F;
    }
    return v;
}

}  // namespace

auto image_primitive_from(double x, double y, const ImageOptions& opt) noexcept -> ImagePrimitive {
    ImagePrimitive p{};
    p.x = x;
    p.y = y;
    p.width_px = safe_dimension(opt.width_px);
    p.height_px = safe_dimension(opt.height_px);
    p.r = opt.r;
    p.g = opt.g;
    p.b = opt.b;
    p.a = opt.a;
    return p;
}

}  // namespace noted::domain::tool
