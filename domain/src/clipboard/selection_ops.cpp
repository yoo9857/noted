#include "noted/domain/clipboard/selection_ops.hpp"

#include "noted/domain/document/document.hpp"
#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/shape_drag.hpp"

namespace noted::domain {

auto shapes_in_selection(const Document& doc, const Selection& sel) -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    if (sel.is_empty()) {
        return result;
    }
    const auto& shapes = doc.shapes();
    result.reserve(shapes.size());
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto& s = shapes[i];
        const auto cx = static_cast<std::int32_t>((s.x0 + s.x1) * 0.5);
        const auto cy = static_cast<std::int32_t>((s.y0 + s.y1) * 0.5);
        if (sel.contains(cx, cy)) {
            result.push_back(i);
        }
    }
    return result;
}

}  // namespace noted::domain
