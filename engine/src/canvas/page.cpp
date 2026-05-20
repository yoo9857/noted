#include "noted/engine/canvas/page.hpp"

#include <algorithm>
#include <cmath>

namespace noted::canvas {

namespace {

// Clamp a user-supplied page extent to a positive floor. The 1 px
// floor matches the same defensive posture Camera::set_canvas_extent
// uses — a zero or non-finite extent breaks every downstream
// pixel-space math (origin reflow, max_width, the future shader's
// pixel → NDC division).
[[nodiscard]] auto safe_extent(float v) noexcept -> float {
    if (std::isnan(v) || v < 1.0F) {
        return 1.0F;
    }
    return v;
}

// Clamp the gap the same way the extent is clamped but allow 0 —
// touching pages are a valid Goodnotes choice ("continuous scroll
// no gap"). Negative / NaN collapse to 0.
[[nodiscard]] auto safe_gap(float v) noexcept -> float {
    if (std::isnan(v) || v < 0.0F) {
        return 0.0F;
    }
    return v;
}

}  // namespace

PageList::PageList(float page_gap_px) noexcept : gap_(safe_gap(page_gap_px)) {}

auto PageList::add_page(float w, float h, PageBackground bg) -> std::size_t {
    Page p{};
    p.extent_w_px = safe_extent(w);
    p.extent_h_px = safe_extent(h);
    p.background = bg;
    // origin_y_px will be set by reflow; leave x at 0 for the
    // standard "shared left edge" layout. Callers that want a
    // different per-page x can adjust the returned page in-place
    // — reflow only touches origin_y_px.
    p.origin_x_px = 0.0F;
    p.origin_y_px = 0.0F;

    pages_.push_back(p);
    reflow_origins();
    return pages_.size() - 1;
}

void PageList::remove_page(std::size_t index) {
    if (index >= pages_.size()) {
        return;
    }
    pages_.erase(pages_.begin() + static_cast<std::ptrdiff_t>(index));
    reflow_origins();
}

auto PageList::insert_page(std::size_t index, const Page& page) -> std::size_t {
    const std::size_t effective = index > pages_.size() ? pages_.size() : index;
    Page p = page;
    // Same extent-clamping discipline as add_page so a malformed
    // restore can't poison the list.
    p.extent_w_px = safe_extent(p.extent_w_px);
    p.extent_h_px = safe_extent(p.extent_h_px);
    // origin_y_px is recomputed by the reflow; origin_x_px is left
    // untouched (matches the "shared left edge with per-page X
    // overrides" contract that the rest of the API observes).
    p.origin_y_px = 0.0F;
    pages_.insert(pages_.begin() + static_cast<std::ptrdiff_t>(effective), p);
    reflow_origins();
    return effective;
}

void PageList::set_page_origin_x(std::size_t index, float x) noexcept {
    if (index >= pages_.size()) {
        return;
    }
    // NaN guard: a stray bad input shouldn't dump the page off
    // the document. The page's X is purely cosmetic (it's how the
    // shader places the quad on canvas), so the safest fallback
    // is "leave it where it is."
    if (x != x) {
        return;
    }
    pages_[index].origin_x_px = x;
}

void PageList::set_gap_px(float gap) noexcept {
    gap_ = safe_gap(gap);
    reflow_origins();
}

auto PageList::total_height_px() const noexcept -> float {
    if (pages_.empty()) {
        return 0.0F;
    }
    float total = 0.0F;
    for (const auto& p : pages_) {
        total += p.extent_h_px;
    }
    // (size - 1) gaps between size pages.
    total += static_cast<float>(pages_.size() - 1U) * gap_;
    return total;
}

auto PageList::max_width_px() const noexcept -> float {
    float w = 0.0F;
    for (const auto& p : pages_) {
        if (p.extent_w_px > w) {
            w = p.extent_w_px;
        }
    }
    return w;
}

void PageList::reflow_origins() noexcept {
    float cursor_y = 0.0F;
    for (auto& p : pages_) {
        p.origin_y_px = cursor_y;
        cursor_y += p.extent_h_px + gap_;
    }
}

}  // namespace noted::canvas
