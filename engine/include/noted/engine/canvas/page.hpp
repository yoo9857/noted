#pragma once

// Page model — the Goodnotes-style multi-page layout primitive.
//
// A `Page` is a fixed-extent rectangle that lives at a known origin
// in the document's canvas-pixel coordinate system. Pages stack
// vertically with a configurable gap, the way Goodnotes / Notability
// / GoodNotes 6 lay out notebooks. Each page carries its own
// background style — blank, lined, grid, dotted — for the future
// renderer pass to draw.
//
// This header is **pure data + pure logic** (Phase A.3.a). It owns
// no GPU resources, has no rendering, and is fully unit-testable.
// The GPU integration (page-background shader, draw call inserted
// into the canvas pass before the layer compositor + stroke engine)
// follows in Phase A.3.b.
//
// Why a separate primitive vs. extending `domain::Document`:
//   - Pages are a **canvas-layout** concept, not a document-tree
//     concept. The block tree (group / text / heading / code /
//     canvas / image / embed) describes content; the page list
//     describes WHERE on the infinite canvas that content lands.
//   - Keeping the two orthogonal lets a future PR cross-link them
//     (e.g. "this PageBlock kind references PageList[i]") without
//     re-shaping either side. Same split LayerGraph + Document
//     already use.
//   - Pure-logic Page math (origin reflow, total height, max
//     width) can be unit-tested without spinning up a GPU.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace noted::canvas {

// Background pattern drawn behind any blocks / ink that land on
// this page. The wire-stable enum ordinals match the file-format
// schema PR A.3.c will introduce — don't renumber.
enum class PageBackground : std::uint8_t {
    blank = 0,
    lined = 1,
    grid = 2,
    dotted = 3,
};

struct Page {
    // Extent in canvas pixels. Default = US Letter at 72 DPI. The
    // PageList helpers don't constrain dimensions — a future "page
    // size presets" UI picks values, the list just stores them.
    float extent_w_px{612.0F};
    float extent_h_px{792.0F};
    PageBackground background{PageBackground::blank};
    // Top-left position in the document's canvas-pixel coordinate
    // system. The PageList computes this from the stacking order;
    // direct mutation is fine but `add_page` / `remove_page` will
    // overwrite it on the next reflow.
    float origin_x_px{0.0F};
    float origin_y_px{0.0F};
};

// Vertically-stacked list of pages with a configurable gap between
// adjacent pages. Goodnotes / Notability style. Pages share the
// left edge (origin_x_px = 0) by default; offsetting individual
// pages is supported but not what the helpers automate.
class PageList {
public:
    // Default gap matches the visual breathing room Goodnotes uses
    // between pages — distinct enough that the eye reads them as
    // separate documents, tight enough that scrolling feels
    // continuous.
    static constexpr float kDefaultGapPx = 24.0F;

    PageList() = default;
    explicit PageList(float page_gap_px) noexcept;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return pages_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return pages_.empty(); }
    [[nodiscard]] auto pages() const noexcept -> const std::vector<Page>& { return pages_; }
    [[nodiscard]] auto gap_px() const noexcept -> float { return gap_; }

    // Append a page at the bottom of the stack. Origin is auto-
    // computed from the previous page's bottom edge + `gap_px`.
    // Returns the new page's index. Negative / non-finite extents
    // are clamped to a 1 px floor so a stray bad input doesn't
    // produce a degenerate page (zero extent breaks the reflow
    // math + later renderer's pixel→NDC conversion).
    auto add_page(float w, float h, PageBackground bg) -> std::size_t;

    // Remove the page at `index` and re-flow every page below it
    // upward (each origin_y_px moves up by the removed page's
    // height + gap). No-op when `index >= size()`.
    void remove_page(std::size_t index);

    // Override a single page's horizontal origin. `add_page` /
    // `remove_page` do not reflow the X axis (pages share a
    // left edge by default), so per-page X overrides survive
    // subsequent list mutations. No-op when `index >= size()`.
    void set_page_origin_x(std::size_t index, float x) noexcept;

    // Total stacked height: sum of page heights + (size - 1) gaps.
    // 0 for an empty list.
    [[nodiscard]] auto total_height_px() const noexcept -> float;

    // Widest page in the list. Useful for the renderer to size a
    // background frame or for the camera's "fit to width" math. 0
    // for an empty list.
    [[nodiscard]] auto max_width_px() const noexcept -> float;

private:
    void reflow_origins() noexcept;

    std::vector<Page> pages_;
    float gap_{kDefaultGapPx};
};

}  // namespace noted::canvas
