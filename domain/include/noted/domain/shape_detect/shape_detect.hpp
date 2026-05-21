#pragma once

// Smart-shape detection — convert a freehand `Stroke` into a recognised
// `ShapePrimitive` (rectangle / ellipse) when the user clearly traced
// one. Goodnotes / Procreate / Notability all have this as their
// signature "draw and hold" or "snap to shape" gesture.
//
// **Layering**: this header lives in `domain/shape_detect/` —
// **pure data + pure logic**. No Vulkan, no I/O, no ImGui. The
// detector takes a `Stroke` (vector ink centerline + brush style)
// and returns either an empty optional (no clear shape) or a
// `DetectionResult` containing the recognised `ShapePrimitive` plus
// a confidence score. The caller (app-layer pen tool, Phase B.5+
// follow-up) decides whether to commit the conversion.
//
// **Why the optional + confidence shape**:
//   - Detection is fundamentally fuzzy — there's always a stroke that
//     "could be" either a circle or an awkward ellipse. Returning a
//     confidence score lets the host pick a threshold appropriate to
//     its UX (e.g. require confidence ≥ 0.7 for auto-conversion, but
//     show all matches in a "convert to…" menu).
//   - `std::optional` makes "no clear match" the type-safe default,
//     so a caller can't accidentally commit a low-confidence result
//     by forgetting to inspect a sentinel value.
//
// **Why exceptions are unnecessary here**:
//   - Every degenerate input (empty stroke, single sample, NaN
//     coords, zero-area bbox) maps cleanly to a domain-level
//     "no detection" — the function returns `nullopt`. There is no
//     I/O, no allocation that can fail meaningfully, no Vulkan handle
//     to leak. The frame-hot path stays exception-free.
//
// **Detector inventory**:
//   - `detect_ellipse` — closed stroke matching an axis-aligned
//     ellipse inscribed in its bounding box.
//   - `detect_rectangle` — closed stroke matching an axis-aligned
//     rectangle aligned with its bounding box.
//
// New detectors (line, polygon, star) drop in as additional free
// functions; `detect_shape` widens its `for` loop. No structural
// change is required.
//
// **Output mapping**: results target the existing wire-stable
// `noted::domain::tool::ShapePrimitive` (used by the Shape tool +
// `.noted` persistence v3+). Smart-shape conversion therefore costs
// **zero** schema bumps — a recognised shape is indistinguishable
// from a hand-drawn shape from the document's point of view.

#include <optional>

#include "noted/domain/tool/shape_drag.hpp"
#include "noted/engine/stroke/stroke_geometry.hpp"

namespace noted::domain::shape_detect {

// Tolerances for the family of detectors. Defaults are tuned for
// pixel space (canvas pixels at zoom = 1×). Callers that need
// per-zoom tolerance can pre-scale before calling.
struct DetectionConfig {
    // Maximum mean perpendicular distance, in canvas pixels, of the
    // stroke samples from the best-fit primitive. Above this, the
    // detector returns no result. Higher = more permissive.
    float tolerance_px{12.0F};

    // For closed shapes (rectangle, ellipse): the gap between the
    // stroke's first and last sample as a fraction of the bbox
    // diagonal. Above this the stroke is considered open and is
    // not a candidate for a closed shape.
    float closure_ratio{0.25F};

    // Below this, no detection is attempted. Coarse motions with
    // < 8 samples produce noisy fits; pencil-and-paper users
    // intending a shape always have many more samples.
    int min_samples{8};

    // Below this, no detection is attempted — a 5-px-diagonal
    // "shape" is almost certainly noise the user didn't intend.
    float min_bbox_diagonal_px{16.0F};

    // Minimum confidence for `detect_shape` to return its
    // best-match candidate. Below this it returns nullopt even if
    // a detector technically matched.
    float min_confidence{0.55F};
};

// One detector's verdict on a stroke.
struct DetectionResult {
    // The recognised primitive, ready to drop into
    // `Document::shapes()` via `AddShapeCommand`. Stroke parameters
    // (colour, width) are copied from the originating stroke's
    // BrushStyle so the recognised shape inherits the user's brush
    // settings. Width is the brush's `max_radius_px` for a visible
    // outline.
    noted::domain::tool::ShapePrimitive shape{};

    // Match quality in [0, 1]. 1.0 = perfect fit; 0.0 = barely
    // distinguishable from a random scribble. Detectors return their
    // best honest assessment — callers pick the threshold for
    // their UX.
    float confidence{0.0F};
};

// Try the full detector battery on `stroke` and return the
// highest-confidence match, or `nullopt` if no detector reaches
// `cfg.min_confidence`.
//
// The returned `ShapePrimitive`'s outline colour + width come from
// the stroke's `BrushStyle` so the recognised shape uses the brush
// the user actually had selected.
//
// Pre-conditions are domain-level only — all degenerate inputs map
// cleanly to `nullopt`:
//   - empty / single-sample / fewer-than-min stroke,
//   - sub-min-diagonal bounding box,
//   - non-finite (NaN / inf) coordinates anywhere,
//   - every detector below threshold.
[[nodiscard]] auto detect_shape(const noted::stroke::Stroke& stroke,
                                const DetectionConfig& cfg = {}) -> std::optional<DetectionResult>;

// Try ellipse detection only. Exposed so the caller can constrain
// detection (e.g. a "force ellipse" UI button) and so each detector
// is independently unit-testable.
//
// Ellipse model: axis-aligned, inscribed in the stroke's bounding
// box. Rotated ellipses are not detected here — they need a
// least-squares conic fit that is its own follow-up.
[[nodiscard]] auto detect_ellipse(const noted::stroke::Stroke& stroke,
                                  const DetectionConfig& cfg) -> std::optional<DetectionResult>;

// Try rectangle detection only. Same axis-aligned restriction as
// ellipse; rotated-rectangle detection is a follow-up.
[[nodiscard]] auto detect_rectangle(const noted::stroke::Stroke& stroke,
                                    const DetectionConfig& cfg) -> std::optional<DetectionResult>;

}  // namespace noted::domain::shape_detect
