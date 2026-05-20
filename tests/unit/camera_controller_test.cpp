#include "input/camera_controller.hpp"

#include <gtest/gtest.h>

#include "noted/engine/canvas/camera.hpp"

#include "config/app_config.hpp"

namespace {

using noted::app::config::CanvasConfig;
using noted::app::input::CameraController;
using noted::canvas::Camera;

[[nodiscard]] auto make_camera_with_extents(std::uint32_t w, std::uint32_t h) -> Camera {
    Camera c;
    c.set_canvas_extent(w, h);
    c.set_window_extent(w, h);
    return c;
}

}  // namespace

// ---- Pan -------------------------------------------------------------------

TEST(CameraController, PressOnNonMiddleDoesNotStartPan) {
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    // inject_move_ goes through the move handler; without a press the
    // controller still tracks the cursor but does NOT pan.
    ctrl.inject_move_(100.0, 100.0);
    ctrl.inject_move_(200.0, 150.0);
    EXPECT_DOUBLE_EQ(cam.translation_x(), 0.0);
    EXPECT_DOUBLE_EQ(cam.translation_y(), 0.0);
    EXPECT_FALSE(ctrl.is_panning());
    EXPECT_DOUBLE_EQ(ctrl.cursor_x(), 200.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_y(), 150.0);
}

TEST(CameraController, MiddlePressMoveTranslatesCamera) {
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    ctrl.inject_press_middle_(100.0, 100.0);
    EXPECT_TRUE(ctrl.is_panning());
    ctrl.inject_move_(150.0, 130.0);
    EXPECT_DOUBLE_EQ(cam.translation_x(), 50.0);
    EXPECT_DOUBLE_EQ(cam.translation_y(), 30.0);
    ctrl.inject_move_(200.0, 200.0);  // accumulates
    EXPECT_DOUBLE_EQ(cam.translation_x(), 100.0);
    EXPECT_DOUBLE_EQ(cam.translation_y(), 100.0);
}

TEST(CameraController, MiddleReleaseStopsPan) {
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    ctrl.inject_press_middle_(0.0, 0.0);
    ctrl.inject_move_(50.0, 50.0);
    EXPECT_DOUBLE_EQ(cam.translation_x(), 50.0);
    ctrl.inject_release_middle_(50.0, 50.0);
    EXPECT_FALSE(ctrl.is_panning());
    ctrl.inject_move_(100.0, 100.0);  // no further translate
    EXPECT_DOUBLE_EQ(cam.translation_x(), 50.0);
    EXPECT_DOUBLE_EQ(cam.translation_y(), 50.0);
}

// ---- Scroll-zoom -----------------------------------------------------------

TEST(CameraController, ScrollZoomUsesCachedCursor) {
    // After a move, the cursor cache is updated. A scroll then anchors
    // the zoom around that point — verified via `zoom_around`'s pin
    // invariant (already covered by camera_test; here we just confirm
    // the controller plumbs the right cursor coords).
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    cfg.zoom_step = 2.0;
    cfg.zoom_min = 0.01;
    cfg.zoom_max = 100.0;
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    ctrl.inject_move_(400.0, 300.0);
    const double anchor_canvas_x = cam.unproject_x(400.0);
    const double anchor_canvas_y = cam.unproject_y(300.0);
    ctrl.inject_scroll_(1.0);  // factor = 2^1 = 2x
    EXPECT_DOUBLE_EQ(cam.scale(), 2.0);
    // The pin invariant: the canvas point that WAS under (400, 300)
    // is still under (400, 300) after the zoom.
    EXPECT_NEAR(cam.unproject_x(400.0), anchor_canvas_x, 1e-9);
    EXPECT_NEAR(cam.unproject_y(300.0), anchor_canvas_y, 1e-9);
}

TEST(CameraController, ScrollClampsToConfigMinMax) {
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    cfg.zoom_step = 10.0;  // aggressive
    cfg.zoom_min = 0.5;
    cfg.zoom_max = 2.0;
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    // 10x in one notch — would land at scale=10, clamp pulls it back
    // to 2.0.
    ctrl.inject_scroll_(1.0);
    EXPECT_DOUBLE_EQ(cam.scale(), cfg.zoom_max);
    // 1/10x in one notch — would land at scale=0.2, clamp pulls back
    // to 0.5.
    ctrl.inject_scroll_(-2.0);
    EXPECT_DOUBLE_EQ(cam.scale(), cfg.zoom_min);
}

// ---- Framebuffer resize ----------------------------------------------------

TEST(CameraController, FramebufferResizeUpdatesCameraExtents) {
    Camera cam = make_camera_with_extents(100, 100);
    CanvasConfig cfg{};
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    ctrl.inject_framebuffer_resize_(1920, 1080);
    EXPECT_DOUBLE_EQ(cam.window_extent_w(), 1920.0);
    EXPECT_DOUBLE_EQ(cam.window_extent_h(), 1080.0);
    EXPECT_DOUBLE_EQ(cam.canvas_extent_w(), 1920.0);
    EXPECT_DOUBLE_EQ(cam.canvas_extent_h(), 1080.0);
}

// ---- Cursor tracking -------------------------------------------------------

TEST(CameraController, CursorCacheTracksLastMove) {
    Camera cam = make_camera_with_extents(1000, 1000);
    CanvasConfig cfg{};
    CameraController ctrl{CameraController::TestingTag{}, cam, cfg};
    EXPECT_DOUBLE_EQ(ctrl.cursor_x(), 0.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_y(), 0.0);
    ctrl.inject_move_(42.0, 7.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_x(), 42.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_y(), 7.0);
    ctrl.inject_move_(-5.0, -3.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_x(), -5.0);
    EXPECT_DOUBLE_EQ(ctrl.cursor_y(), -3.0);
}
