#pragma once

// Selection-on-primitives helpers — the small piece of logic that
// turns "the user has this Selection (rect set)" into "these
// primitives in the Document are selected". Lives alongside the
// Clipboard because Cut / Copy commands need the same predicate
// the Delete-selection gesture does.
//
// Pure-domain: no GPU, no UI, no I/O. Just geometric tests against
// the existing `Selection` rect set + Document primitive bounds.
//
// MVP scope: shapes only (v1 of the clipboard). Text / image /
// stroke primitive selection lands in follow-up PRs.

#include <cstddef>
#include <vector>

namespace noted::domain {

class Document;
class Selection;

// Returns the indices of every shape whose centre point lies inside
// any of the Selection's rectangles. Centre-based test mirrors the
// "marquee feels right" UX of every Photoshop-like editor: the
// selection rect doesn't need to fully enclose a shape's bounds —
// the user grabs whatever they enclose the middle of.
//
// Indices are returned in ASCENDING order so callers can pass them
// directly to `DeleteShapesCommand` (which sorts internally but is
// happy with already-sorted input).
[[nodiscard]] auto shapes_in_selection(const Document& doc,
                                       const Selection& sel) -> std::vector<std::size_t>;

}  // namespace noted::domain
