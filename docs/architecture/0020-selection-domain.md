# ADR 0020: Selection domain — rect-list set algebra

**Status:** Accepted
**Date:** 2026-05-16

## Context

The Photoshop side now has a layer graph (ADR 0016) and a GPU compositor
(ADR 0019). The remaining missing piece to make non-destructive editing
useful is **selection** — the user's way of saying "operate only here".
Brush strokes, fill, adjustment-layer effects, and image-edit ops all
need to consult a per-document selection.

Selections in Photoshop-class tools come from many sources — marquee,
lasso, polygon, magic-wand, color range, mask painting — but they all
end up as one of two representations downstream:

1. **Geometric set** — rectangles, polygons, or a CSG of those. Cheap
   to manipulate, exact, easy to undo.
2. **Pixel mask** — a 1-channel raster that gates per-pixel operations.
   Cheap to sample on GPU, supports arbitrary shape, but rasterizing
   complex geometry every edit is expensive.

Real apps keep both: geometry on the CPU for undo / set operations / UI
overlays, and a rasterized mask on the GPU for fragment-shader gating.
The split lets each side use the representation that matches its
access pattern.

This ADR ships the **CPU geometric set** half. A follow-up PR adds the
GPU mask + a rasterizer that consumes a `Selection` and writes an R8
mask image; another follow-up wires the mask into the layer
compositor.

## Decision

### Module placement

New namespace under the existing `domain/` module:
`noted::domain::selection`. Header lives at
`domain/include/noted/domain/selection/selection.hpp`,
implementation at `domain/src/selection/selection.cpp`. The `domain`
module is the right home — `Selection` is pure data (no GPU, no
platform), it sits alongside `LayerGraph` and the document model, and
it serializes into the same `.noted` file format eventually.

This matches the precedent set by ADR 0016 → ADR 0019:
data in `domain`, GPU consumption layer in a separate module
(compositor / future `gpu/selection_mask`).

### Type model

```cpp
struct SelectionRect {
    std::int32_t x, y, width, height;     // top-left + extent
    bool is_empty() const noexcept;       // width <= 0 || height <= 0
    int32_t right() / bottom() const noexcept;
    bool contains(int32_t px, int32_t py) const noexcept;  // half-open
};

auto intersect(SelectionRect, SelectionRect) noexcept
    -> std::optional<SelectionRect>;

class Selection {
    Selection() = default;
    static auto from_rect(SelectionRect) -> Selection;

    bool is_empty() const noexcept;
    auto rects() const noexcept -> const std::vector<SelectionRect>&;
    auto bounds() const noexcept -> std::optional<SelectionRect>;
    bool contains(int32_t x, int32_t y) const noexcept;

    Selection& add_rect(SelectionRect);
    Selection& intersect_rect(SelectionRect);
    Selection& subtract_rect(SelectionRect);
    void clear() noexcept;
};
```

### MVP scope: rectangles only

Lasso / polygon / magic-wand selections rasterize *to* a rect cover
or a polygon edge list later, but the v1 data model is just a list of
axis-aligned integer rectangles whose union is the selection.

Why this is enough for the next several PRs:
- Marquee selection drops in as a single `from_rect`.
- Compositor masking takes a `Selection` and walks `rects()` to
  rasterize.
- Undo / redo round-trips a small POD list — cheap.
- Set operations stay closed under rectangles, so the data model
  doesn't fight typical UI gestures.

When polygon / lasso geometry lands, it ships as a separate
`SelectionPolygon` and `Selection` gains a sum-type. The rect-list
fast path stays, and the polygon path triangulates / rasterizes
once on transition to the GPU mask.

### Canonical normalization

After every mutator the internal list is:
1. **Free of empty rects** (`width > 0 && height > 0`).
2. **Sorted** by `(y, x, height, width)` (matches `std::tie` on the
   fields the rasterizer will use as raster scan order).
3. **Deduplicated** by exact equality (after sort).

The list is **not** guaranteed disjoint. Two overlapping rects in the
same selection are allowed because the future rasterizer renders each
rect into the mask via additive blend; the union semantic falls out
without us paying to compute a disjoint cover on the CPU.

Equivalent selections compare equal via the default
`operator==` on the canonical list. Tests rely on this.

### Set operations

- `add_rect(r)`: append (if non-empty), normalize. Overlapping rects
  stay separate — they collapse to a single mask blob at raster time.
- `intersect_rect(r)`: each existing rect is replaced by its
  intersection with `r`; non-overlapping rects drop out. Empty `r`
  clears the whole selection.
- `subtract_rect(r)`: each existing rect overlapping `r` is partitioned
  into up to four bands (top / bottom full-width strips + left / right
  intra-overlap strips). Non-overlapping rects pass through. This is
  the "knife cut" that produces the visible hole in subtractive
  selection UI gestures.

All three return `*this` to support chaining.

### Why this list-of-rects representation

- **Bitmap representation now** would force every domain operation
  through the GPU and complicate undo (a 4K document's selection
  bitmap is 16 MB — cheap to swap on GPU, expensive in command-list
  history).
- **Polygon now** is the wrong shape for marquee + UI gesture flows,
  and the rect-list satisfies every operation the next several PRs
  need.
- **CSG tree (`union` / `intersect` / `difference` nodes)** is more
  general but every traversal pays for the structure. For axis-aligned
  rects, a normalized list IS the canonical form.

## Alternatives considered

- **Disjoint rect decomposition** (every mutator splits overlaps so
  the list is a partition of the selection). Cheaper raster, but every
  add becomes O(n²) and the structure visible to the UI gets fragmentary
  — a 5-rect selection might show 17 rects after some adds. Rejected.
- **Region as polygon edge list.** Right answer for lasso. Premature
  for marquee MVP and forces an early rasterization pipeline. Defer.
- **GPU bitmap as the single source of truth.** Conflicts with undo /
  history / persistence requirements (the bitmap is huge and lives in
  GPU memory). Rejected.
- **Throw on bad input** (negative dimensions, integer overflow).
  Conflicts with ADR 0003. `Selection` accepts any `SelectionRect`
  and silently drops the empty ones.

## Consequences

- `domain::Selection` joins `LayerGraph` as the second concrete domain
  type. Both share the "pure data, value semantics, every mutator
  returns Result<T> or chainable reference" idiom.
- 20 new unit tests cover: `SelectionRect` predicates, geometric
  `intersect`, `Selection` construction / add / bounds / contains,
  intersect, subtract (full cover / no overlap / center hole / corner
  L-cut), normalization (empty filter, canonical sort, dedup),
  equality, and chained set-op round-trips.
- `domain/CMakeLists.txt` picks up the new source file.
- The compositor / stroke engine are unchanged for now — they consume
  the future GPU mask, not this domain type directly.

## Follow-ups

- **`gpu::SelectionMask`** — R8_UNORM single-channel image with
  `transition_to` and a `fill_rect(cb, rect, value)` helper. Same
  RAII shape as `CanvasRenderTarget`.
- **`SelectionRasterizer`** — takes a `Selection` + a
  `gpu::SelectionMask` + a command buffer, fills the mask with the
  selection's geometry via the layer compositor's pipeline pattern
  (or a dedicated `select.slang` fragment that writes 1.0 inside the
  rect, 0.0 outside).
- **Compositor masking** — `LayerCompositor::composite()` takes an
  optional `SelectionMask&`; the layer shader multiplies its output
  alpha by the mask sample. Falls back to "no mask" when omitted.
- **Polygon / lasso selections** — `SelectionPolygon` type plus
  `Selection` upgraded to a sum type. Triangulation / rasterization
  is one-time on the transition to the GPU mask.
- **Marquee / lasso UI gestures** — needs the UI stack ADR first.
