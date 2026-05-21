#include "noted/domain/selection/selection.hpp"

#include <algorithm>
#include <tuple>

namespace noted::domain {

auto intersect(SelectionRect a, SelectionRect b) noexcept -> std::optional<SelectionRect> {
    if (a.is_empty() || b.is_empty()) {
        return std::nullopt;
    }
    const auto x1 = std::max(a.x, b.x);
    const auto y1 = std::max(a.y, b.y);
    const auto x2 = std::min(a.right(), b.right());
    const auto y2 = std::min(a.bottom(), b.bottom());
    if (x2 <= x1 || y2 <= y1) {
        return std::nullopt;
    }
    return SelectionRect{x1, y1, x2 - x1, y2 - y1};
}

auto Selection::from_rect(SelectionRect r) -> Selection {
    Selection s;
    if (!r.is_empty()) {
        s.rects_.push_back(r);
    }
    return s;
}

auto Selection::from_polygon(LassoPolygon p) -> Selection {
    Selection s;
    if (!p.is_empty()) {
        s.polygons_.push_back(std::move(p));
    }
    return s;
}

auto Selection::bounds() const noexcept -> std::optional<SelectionRect> {
    if (rects_.empty() && polygons_.empty()) {
        return std::nullopt;
    }
    bool seeded = false;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t x2 = 0;
    std::int32_t y2 = 0;
    const auto extend = [&](SelectionRect r) {
        if (!seeded) {
            x1 = r.x;
            y1 = r.y;
            x2 = r.right();
            y2 = r.bottom();
            seeded = true;
        } else {
            x1 = std::min(x1, r.x);
            y1 = std::min(y1, r.y);
            x2 = std::max(x2, r.right());
            y2 = std::max(y2, r.bottom());
        }
    };
    for (const auto& r : rects_) {
        extend(r);
    }
    for (const auto& p : polygons_) {
        if (auto bb = p.aabb()) {
            extend(*bb);
        }
    }
    if (!seeded) {
        return std::nullopt;
    }
    return SelectionRect{x1, y1, x2 - x1, y2 - y1};
}

auto Selection::contains(std::int32_t x, std::int32_t y) const noexcept -> bool {
    for (const auto& r : rects_) {
        if (r.contains(x, y)) {
            return true;
        }
    }
    for (const auto& p : polygons_) {
        if (p.contains(x, y)) {
            return true;
        }
    }
    return false;
}

auto Selection::add_rect(SelectionRect r) -> Selection& {
    if (!r.is_empty()) {
        rects_.push_back(r);
    }
    normalize_();
    return *this;
}

auto Selection::add_polygon(LassoPolygon p) -> Selection& {
    if (!p.is_empty()) {
        polygons_.push_back(std::move(p));
    }
    // No polygon normalize today — vertex equality / canonicalisation
    // is expensive and the rasterizer is happy with duplicates. The
    // rect normalize_ still runs to keep the rect side canonical.
    normalize_();
    return *this;
}

auto Selection::intersect_rect(SelectionRect r) -> Selection& {
    if (r.is_empty()) {
        rects_.clear();
        return *this;
    }
    std::vector<SelectionRect> out;
    out.reserve(rects_.size());
    for (const auto& existing : rects_) {
        if (auto i = intersect(existing, r)) {
            out.push_back(*i);
        }
    }
    rects_ = std::move(out);
    normalize_();
    return *this;
}

auto Selection::subtract_rect(SelectionRect r) -> Selection& {
    if (r.is_empty()) {
        return *this;
    }
    std::vector<SelectionRect> out;
    out.reserve(rects_.size() * 4);  // worst-case 4 pieces per rect
    for (const auto& existing : rects_) {
        const auto inter = intersect(existing, r);
        if (!inter) {
            out.push_back(existing);
            continue;
        }
        // Partition `existing` into the up-to-four bands that lie
        // outside `inter` but inside `existing`:
        //   top:    full-width band above inter
        //   bottom: full-width band below inter
        //   left:   inter-height strip left of inter
        //   right:  inter-height strip right of inter
        if (existing.y < inter->y) {
            out.push_back({existing.x, existing.y, existing.width, inter->y - existing.y});
        }
        if (inter->bottom() < existing.bottom()) {
            out.push_back(
                {existing.x, inter->bottom(), existing.width, existing.bottom() - inter->bottom()});
        }
        if (existing.x < inter->x) {
            out.push_back({existing.x, inter->y, inter->x - existing.x, inter->height});
        }
        if (inter->right() < existing.right()) {
            out.push_back(
                {inter->right(), inter->y, existing.right() - inter->right(), inter->height});
        }
    }
    rects_ = std::move(out);
    normalize_();
    return *this;
}

void Selection::normalize_() {
    // Drop empties.
    const auto end = std::remove_if(
        rects_.begin(), rects_.end(), [](const SelectionRect& r) { return r.is_empty(); });
    rects_.erase(end, rects_.end());
    // Canonical order: (y, x, height, width) so two equivalent normalized
    // selections compare equal via the default operator==.
    std::sort(rects_.begin(), rects_.end(), [](const SelectionRect& a, const SelectionRect& b) {
        return std::tie(a.y, a.x, a.height, a.width) < std::tie(b.y, b.x, b.height, b.width);
    });
    // Dedup exact duplicates (after sort).
    rects_.erase(std::unique(rects_.begin(), rects_.end()), rects_.end());
}

// ---- LassoPolygon ----------------------------------------------------------

auto LassoPolygon::aabb() const noexcept -> std::optional<SelectionRect> {
    if (vertices_.empty()) {
        return std::nullopt;
    }
    std::int32_t min_x = vertices_[0].x;
    std::int32_t min_y = vertices_[0].y;
    std::int32_t max_x = vertices_[0].x;
    std::int32_t max_y = vertices_[0].y;
    for (const auto& v : vertices_) {
        min_x = std::min(min_x, v.x);
        min_y = std::min(min_y, v.y);
        max_x = std::max(max_x, v.x);
        max_y = std::max(max_y, v.y);
    }
    SelectionRect out{min_x, min_y, max_x - min_x, max_y - min_y};
    if (out.is_empty()) {
        return std::nullopt;
    }
    return out;
}

auto LassoPolygon::contains(std::int32_t x, std::int32_t y) const noexcept -> bool {
    if (is_empty()) {
        return false;
    }
    // Standard odd-even ray-cast (Jordan curve theorem). Edges are
    // treated as half-open in y to avoid double-counting vertices
    // sitting on the scan line.
    bool inside = false;
    const std::size_t n = vertices_.size();
    std::size_t j = n - 1;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& vi = vertices_[i];
        const auto& vj = vertices_[j];
        const bool y_split = (vi.y <= y) != (vj.y <= y);
        if (y_split) {
            const double t = static_cast<double>(y - vi.y) / static_cast<double>(vj.y - vi.y);
            const double xi = static_cast<double>(vi.x) + t * static_cast<double>(vj.x - vi.x);
            if (static_cast<double>(x) < xi) {
                inside = !inside;
            }
        }
        j = i;
    }
    return inside;
}

}  // namespace noted::domain
