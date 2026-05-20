#include "noted/domain/tool/tool.hpp"

namespace noted::domain::tool {

auto label(ToolKind k) noexcept -> std::string_view {
    switch (k) {
        case ToolKind::pen:
            return "Pen";
        case ToolKind::eraser:
            return "Eraser";
        case ToolKind::select:
            return "Select";
        case ToolKind::shape:
            return "Shape";
        case ToolKind::text:
            return "Text";
        case ToolKind::image:
            return "Image";
    }
    return "Pen";
}

}  // namespace noted::domain::tool
