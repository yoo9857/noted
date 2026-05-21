#pragma once

// Selection model — the domain side of "what pixels does the user mean?"
//
// A `Selection` is a set of axis-aligned integer rectangles whose union
// describes the active region of the document. The renderer's selection
// mask (raster R8 image) is produced by rasterizing this geometry; the
// compositor / stroke engine consult the mask to gate per-pixel work.
//
// MVP scope:
//   - Rectangles only. Lasso / polygon / magic-wand selections rasterize
//     to a rect cover later; for v1 the data model is "0..N rectangles".
//   - Set operations: union (add), intersect, subtract.
//   - Coordinates are integer pixels in canvas space (matches the rest
//     of the codebase's coordinate convention).
//   - Bounds computation and contains() predicate for quick UI hit-tests.
//
// `Selection` keeps an internally-normalized rect list:
//   - No empty rects.
//   - Rects are clipped to non-negative width/height.
//   - The list is canonical (sorted by (y, x, height, width), no exact
//     duplicates) but NOT guaranteed disjoint. Overlapping rects are
//     allowed because the rasterizer renders each one with additive
//     blending; the union semantic falls out naturally.
//
// What lives in this header is **pure data**. No GPU, no I/O, no
// platform. The GPU-side selection mask + rasterization lands in a
// follow-up PR.
//
// Rationale: see docs/architecture/0020-selection-domain.md.

#include <cstdint>
#include <optional>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::domain {

// Axis-aligned integer rectangle. `x`, `y` are the inclusive top-left;
// `width`, `height` are the extent. An empty rect (width <= 0 or
// height <= 0) is a valid value but is filtered out of `Selection`'s
// internal list.
struct SelectionRect {
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t width{0};
    std::int32_t height{0};

    [[nodiscard]] auto is_empty() const noexcept -> bool { return width <= 0 || height <= 0; }
    [[nodiscard]] auto right() const noexcept -> std::int32_t { return x + width; }
    [[nodiscard]] auto bottom() const noexcept -> std::int32_t { return y + height; }
    [[nodiscard]] auto contains(std::int32_t px, std::int32_t py) const noexcept -> bool {
        return px >= x && px < right() && py >= y && py < bottom();
    }

    [[nodiscard]] auto operator==(const SelectionRect& other) const noexcept -> bool = default;
};

// Geometric intersection of two rectangles. Returns nullopt if they
// don't overlap (or either is empty).
[[nodiscard]] auto intersect(SelectionRect a,
                             SelectionRect b) noexcept -> std::optional<SelectionRect>;

// 2D integer point used by `LassoPolygon`. Lives in the same header
// to avoid an avoidable include / forward-declare dance.
struct Point2i {
    std::int32_t x{0};
    std::int32_t y{0};
    [[nodiscard]] auto operator==(const Point2i&) const noexcept -> bool = default;
};

// Free-form polygon used by the lasso tool to describe a non-
// rectangular selection. Closed by convention — the last vertex is
// implicitly connected to the first; the closing vertex is NOT
// duplicated. Stored alongside the rectangle list inside `Selection`;
// the union of both is the effective selected region.
class LassoPolygon {
public:
    LassoPolygon() = default;
    explicit LassoPolygon(std::vector<Point2i> vertices) noexcept
        : vertices_(std::move(vertices)) {}

    [[nodiscard]] auto vertices() const noexcept -> const std::vector<Point2i>& {
        return vertices_;
    }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return vertices_.size(); }

    // Fewer than 3 vertices = degenerate (line segment can't enclose
    // area). Such polygons answer `contains() == false` for every
    // point.
    [[nodiscard]] auto is_empty() const noexcept -> bool { return vertices_.size() < 3; }

    [[nodiscard]] auto aabb() const noexcept -> std::optional<SelectionRect>;

    // Point-in-polygon via odd-even ray-cast (Jordan curve theorem).
    // O(n) per call; edges are treated as half-open in y to avoid
    // double-counting vertices on the scan line — the standard
    // scanline-fill convention.
    [[nodiscard]] auto contains(std::int32_t x, std::int32_t y) const noexcept -> bool;

    void push(Point2i p) { vertices_.push_back(p); }
    void clear() noexcept { vertices_.clear(); }

    [[nodiscard]] auto operator==(const LassoPolygon&) const noexcept -> bool = default;

private:
    std::vector<Point2i> vertices_;
};

// Set of rectangles whose union is the selected region.
//
// All mutators normalize the internal list (drop empties, sort, dedup
// exact duplicates) so the representation is canonical and tests can
// compare two equivalent selections by `rects()`.
class Selection {
public:
    Selection() = default;
    Selection(const Selection&) = default;
    auto operator=(const Selection&) -> Selection& = default;
    Selection(Selection&&) noexcept = default;
    auto operator=(Selection&&) noexcept -> Selection& = default;
    ~Selection() = default;

    // Build a single-rect selection. Empty rect → empty selection.
    [[nodiscard]] static auto from_rect(SelectionRect r) -> Selection;

    // Build a single-polygon selection. Empty polygon (<3 vertices)
    // → empty selection.
    [[nodiscard]] static auto from_polygon(LassoPolygon p) -> Selection;

    // Empty when there are neither rects nor polygons.
    [[nodiscard]] auto is_empty() const noexcept -> bool {
        return rects_.empty() && polygons_.empty();
    }

    [[nodiscard]] auto rects() const noexcept -> const std::vector<SelectionRect>& {
        return rects_;
    }
    [[nodiscard]] auto polygons() const noexcept -> const std::vector<LassoPolygon>& {
        return polygons_;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return rects_.size() + polygons_.size();
    }

    // Tightest enclosing rect over BOTH rects and polygon AABBs.
    // Empty selection returns nullopt.
    [[nodiscard]] auto bounds() const noexcept -> std::optional<SelectionRect>;

    // True if (x, y) lies inside any constituent rect OR polygon.
    [[nodiscard]] auto contains(std::int32_t x, std::int32_t y) const noexcept -> bool;

    // ---- Set operations ----------------------------------------------------
    // Each mutator returns *this for chaining.

    // Add a rect. Overlapping rects are kept as-is — rasterization
    // handles the union via additive blend.
    auto add_rect(SelectionRect r) -> Selection&;

    // Replace the selection with `this ∩ r`. Every existing rect is
    // intersected with `r` and the non-empty results are kept.
    auto intersect_rect(SelectionRect r) -> Selection&;

    // Replace the selection with `this \ r`. Each existing rect is
    // partitioned around `r` into up to four surviving pieces (top /
    // bottom bands + left / right side strips of the overlap).
    auto subtract_rect(SelectionRect r) -> Selection&;

    // Add a polygon. Stored as-is; rasterizer handles the union.
    auto add_polygon(LassoPolygon p) -> Selection&;

    void clear() noexcept {
        rects_.clear();
        polygons_.clear();
    }

    [[nodiscard]] auto operator==(const Selection& other) const noexcept -> bool {
        return rects_ == other.rects_ && polygons_ == other.polygons_;
    }

private:
    // Normalize: drop empty rects, sort by (y, x, height, width), dedup
    // exact duplicates. Called by every mutator.
    void normalize_();

    std::vector<SelectionRect> rects_;
    std::vector<LassoPolygon> polygons_;
};

}  // namespace noted::domain
