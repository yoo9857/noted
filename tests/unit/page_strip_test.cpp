#include "noted/ui/widget/page_strip.hpp"

#include <gtest/gtest.h>

#include "noted/engine/canvas/page.hpp"

namespace {

using noted::canvas::PageBackground;
using noted::canvas::PageList;
using noted::ui::widget::camera_translation_y_for_page;

// Build a list of `n` identical 100×200 pages with a 10 px gap so
// page i lives at origin_y = i * 210.
[[nodiscard]] auto build_stack(std::size_t n) -> PageList {
    PageList list{10.0F};
    for (std::size_t i = 0; i < n; ++i) {
        list.add_page(100.0F, 200.0F, PageBackground::blank);
    }
    return list;
}

}  // namespace

TEST(PageStripCameraMath, FirstPageLandsAtTargetExactly) {
    const auto pages = build_stack(3);
    // Page 0 lives at origin_y = 0, so target translation = target_y.
    const double t = camera_translation_y_for_page(pages,
                                                   /*index=*/0,
                                                   /*current=*/-9999.0,
                                                   /*scale=*/1.0,
                                                   /*target_screen_y_px=*/80.0);
    EXPECT_DOUBLE_EQ(t, 80.0);
}

TEST(PageStripCameraMath, SecondPageOffsetByOriginAndScale) {
    const auto pages = build_stack(3);
    // Page 1 lives at origin_y = 210. At scale = 1.0:
    //   t = 80 - 210 * 1.0 = -130.
    const double t = camera_translation_y_for_page(pages,
                                                   /*index=*/1,
                                                   /*current=*/0.0,
                                                   /*scale=*/1.0,
                                                   /*target_screen_y_px=*/80.0);
    EXPECT_DOUBLE_EQ(t, -130.0);
}

TEST(PageStripCameraMath, ScaleMultipliesOrigin) {
    const auto pages = build_stack(3);
    // Page 2 lives at origin_y = 420. At scale = 0.5:
    //   t = 80 - 420 * 0.5 = -130.
    const double t = camera_translation_y_for_page(pages,
                                                   /*index=*/2,
                                                   /*current=*/0.0,
                                                   /*scale=*/0.5,
                                                   /*target_screen_y_px=*/80.0);
    EXPECT_DOUBLE_EQ(t, -130.0);
}

TEST(PageStripCameraMath, ProjectionInvariantAfterFocus) {
    // After focusing on page i, the page's top edge in canvas space
    // (origin_y_px) must project to exactly target_screen_y_px.
    const auto pages = build_stack(4);
    constexpr double kScale = 1.75;
    constexpr double kTarget = 120.0;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const double t = camera_translation_y_for_page(pages, i, 0.0, kScale, kTarget);
        const double projected = t + pages.pages()[i].origin_y_px * kScale;
        EXPECT_NEAR(projected, kTarget, 1e-9) << "index=" << i;
    }
}

TEST(PageStripCameraMath, OutOfRangeReturnsCurrentTranslation) {
    const auto pages = build_stack(2);
    EXPECT_DOUBLE_EQ(camera_translation_y_for_page(pages, /*index=*/2, 42.5, 1.0, 80.0), 42.5);
    EXPECT_DOUBLE_EQ(camera_translation_y_for_page(pages, /*index=*/999, -17.0, 1.0, 80.0), -17.0);
}

TEST(PageStripCameraMath, EmptyListReturnsCurrentTranslation) {
    PageList empty;
    EXPECT_DOUBLE_EQ(camera_translation_y_for_page(empty, /*index=*/0, 7.25, 1.0, 80.0), 7.25);
}
