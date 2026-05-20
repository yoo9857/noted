#pragma once

// Page strip — side-rail navigator over a `noted::canvas::PageList`.
//
// Renders one row per page (index + "Page N" + background glyph +
// thumbnail placeholder) inside an ImGui window pinned to the left
// edge. Footer button "+ Add page" appends a new page; right-clicking
// a row opens a context menu with "Remove". Clicking a row emits a
// focus request the host turns into a camera jump.
//
// The widget is **stateless** — it surfaces user intent as a
// `PageStripResult` the host drains every frame, the same pattern
// `menu_bar()` uses. PageList ownership stays with the host (App in
// v0.x; Document in A.3.d).
//
// The "focus index → camera translation_y" math is extracted as a
// pure helper so it can be unit-tested without ImGui. The widget's
// own ImGui calls are interactive-only.

#include <cstddef>
#include <optional>

namespace noted::canvas {
class PageList;
}  // namespace noted::canvas

namespace noted::ui::widget {

// One-frame outputs from page_strip(). At most one mutation field is
// set per frame because ImGui's MenuItem / Button calls only fire on
// the click frame. focus_request can co-occur with add_request when
// the user clicks "+ Add page" — the host should sequence: mutate
// first, then re-resolve focus against the new list.
struct PageStripResult {
    // The user clicked a page row. Index is in `[0, list.size())` at
    // the moment of the click — if the host has already mutated the
    // list this frame, it must re-validate.
    std::optional<std::size_t> focus_request{};
    // The user clicked the "+ Add page" footer button.
    bool add_request{false};
    // The user picked "Remove" from a row's context menu. Index is in
    // `[0, list.size())` at click time.
    std::optional<std::size_t> remove_request{};
};

// Render the page strip side rail inside an ImGui::Begin / End scope
// managed by this function. `open` controls visibility — pass
// `&state.show_page_strip` from menu_bar's MenuBarState so View →
// Page strip toggles it.
//
// `pages` is const — the widget never mutates the list directly.
// Mutations are surfaced via the return value and applied by the
// host (so the host can route them through Command<> in A.3.d).
[[nodiscard]] auto page_strip(const noted::canvas::PageList& pages, bool* open) -> PageStripResult;

// Pure-logic helper extracted for unit-testability — no ImGui, no
// camera type dependency. Computes the camera `translation_y` value
// that puts page `index`'s top edge at `target_screen_y_px` (a small
// breathing-room offset from the top of the window, typically the
// menu bar height + a margin).
//
// `current_translation_y` is returned unchanged when `index` is out
// of range — a click on a stale row should be a no-op, not a jump to
// (0, 0).
//
// Math: screen_y = translation_y + canvas_y * scale. Solving for the
// translation that lands `pages[index].origin_y_px` at the target
// screen y: translation_y = target_screen_y_px - origin_y_px * scale.
[[nodiscard]] auto camera_translation_y_for_page(const noted::canvas::PageList& pages,
                                                 std::size_t index,
                                                 double current_translation_y,
                                                 double scale,
                                                 double target_screen_y_px) noexcept -> double;

}  // namespace noted::ui::widget
