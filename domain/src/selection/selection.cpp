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

auto Selection::bounds() const noexcept -> std::optional<SelectionRect> {
    if (rects_.empty()) {
        return std::nullopt;
    }
    auto x1 = rects_[0].x;
    auto y1 = rects_[0].y;
    auto x2 = rects_[0].right();
    auto y2 = rects_[0].bottom();
    for (std::size_t i = 1; i < rects_.size(); ++i) {
        x1 = std::min(x1, rects_[i].x);
        y1 = std::min(y1, rects_[i].y);
        x2 = std::max(x2, rects_[i].right());
        y2 = std::max(y2, rects_[i].bottom());
    }
    return SelectionRect{x1, y1, x2 - x1, y2 - y1};
}

auto Selection::contains(std::int32_t x, std::int32_t y) const noexcept -> bool {
    for (const auto& r : rects_) {
        if (r.contains(x, y)) {
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

}  // namespace noted::domain
