#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "noted/domain/tool/tool.hpp"

// App-private headers — `app/src` is on the include path for the app
// binary AND for the unit-test binary so internal classes can be
// exercised. See tests/unit/CMakeLists.txt + app/CMakeLists.txt's
// PRIVATE include_directories.
#include "input/tool_input_handler.hpp"
#include "input/tool_input_router.hpp"

namespace {

using noted::app::input::ToolInputHandler;
using noted::app::input::ToolInputRouter;
using noted::domain::tool::ToolKind;

// Mock handler — counts dispatches so tests can pin which handler
// received which event. One mock per `kind`.
class MockHandler final : public ToolInputHandler {
public:
    explicit MockHandler(ToolKind kind) noexcept : kind_{kind} {}

    [[nodiscard]] auto handled_kind() const noexcept -> ToolKind override { return kind_; }
    void on_pressed(double cx, double cy, bool shift, bool alt) override {
        ++press_count;
        last_cx = cx;
        last_cy = cy;
        last_shift = shift;
        last_alt = alt;
    }
    void on_moved(double cx, double cy) override {
        ++move_count;
        last_cx = cx;
        last_cy = cy;
    }
    void on_released(double cx, double cy) override {
        ++release_count;
        last_cx = cx;
        last_cy = cy;
    }
    void on_deactivated() noexcept override { ++deactivate_count; }

    ToolKind kind_;
    int press_count{0};
    int move_count{0};
    int release_count{0};
    int deactivate_count{0};
    double last_cx{0.0};
    double last_cy{0.0};
    bool last_shift{false};
    bool last_alt{false};
};

[[nodiscard]] auto make_router() {
    return std::unique_ptr<ToolInputRouter>(new ToolInputRouter{ToolInputRouter::TestingTag{}});
}

}  // namespace

// ---- Registration + activation --------------------------------------------

TEST(ToolInputRouter, ActiveStartsNull) {
    auto r = make_router();
    EXPECT_EQ(r->active(), nullptr);
}

TEST(ToolInputRouter, ActivatingUnregisteredKindLeavesActiveNull) {
    auto r = make_router();
    r->set_active(ToolKind::pen);
    EXPECT_EQ(r->active(), nullptr);
}

TEST(ToolInputRouter, RegisterThenActivate) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    r->set_active(ToolKind::select);
    EXPECT_EQ(r->active(), raw);
}

TEST(ToolInputRouter, DuplicateRegistrationDropsLater) {
    // First registration wins — second silently no-ops. Documents the
    // contract in router.cpp's comment.
    auto r = make_router();
    auto first = std::make_unique<MockHandler>(ToolKind::select);
    auto* first_raw = first.get();
    auto second = std::make_unique<MockHandler>(ToolKind::select);
    r->register_handler(std::move(first));
    r->register_handler(std::move(second));
    r->set_active(ToolKind::select);
    EXPECT_EQ(r->active(), first_raw);
}

// ---- Dispatch --------------------------------------------------------------

TEST(ToolInputRouter, PressIsDispatchedToActiveHandler) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    r->set_active(ToolKind::select);
    r->inject_press_(50.0, 60.0, /*shift=*/true, /*alt=*/false);
    EXPECT_EQ(raw->press_count, 1);
    EXPECT_DOUBLE_EQ(raw->last_cx, 50.0);
    EXPECT_DOUBLE_EQ(raw->last_cy, 60.0);
    EXPECT_TRUE(raw->last_shift);
    EXPECT_FALSE(raw->last_alt);
}

TEST(ToolInputRouter, MoveAndReleaseDispatchedToSameHandler) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    r->set_active(ToolKind::select);
    r->inject_press_(10.0, 20.0, false, false);
    r->inject_move_(30.0, 40.0);
    r->inject_release_(35.0, 45.0);
    EXPECT_EQ(raw->press_count, 1);
    EXPECT_EQ(raw->move_count, 1);
    EXPECT_EQ(raw->release_count, 1);
    EXPECT_DOUBLE_EQ(raw->last_cx, 35.0);
    EXPECT_DOUBLE_EQ(raw->last_cy, 45.0);
}

TEST(ToolInputRouter, NoDispatchWhenNoActiveHandler) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    // Don't activate. Press should be dropped on the floor — no
    // handler is currently selected.
    r->inject_press_(50.0, 60.0, false, false);
    r->inject_move_(70.0, 80.0);
    r->inject_release_(70.0, 80.0);
    EXPECT_EQ(raw->press_count, 0);
    EXPECT_EQ(raw->move_count, 0);
    EXPECT_EQ(raw->release_count, 0);
}

TEST(ToolInputRouter, OnlyTheActiveHandlerSeesEvents) {
    auto r = make_router();
    auto pen_h = std::make_unique<MockHandler>(ToolKind::pen);
    auto sel_h = std::make_unique<MockHandler>(ToolKind::select);
    auto* pen_raw = pen_h.get();
    auto* sel_raw = sel_h.get();
    r->register_handler(std::move(pen_h));
    r->register_handler(std::move(sel_h));
    r->set_active(ToolKind::select);
    r->inject_press_(10.0, 10.0, false, false);
    EXPECT_EQ(sel_raw->press_count, 1);
    EXPECT_EQ(pen_raw->press_count, 0);
}

// ---- Switch semantics ------------------------------------------------------

TEST(ToolInputRouter, SwitchingActiveTriggersDeactivateOnPrevious) {
    auto r = make_router();
    auto pen_h = std::make_unique<MockHandler>(ToolKind::pen);
    auto sel_h = std::make_unique<MockHandler>(ToolKind::select);
    auto* pen_raw = pen_h.get();
    auto* sel_raw = sel_h.get();
    r->register_handler(std::move(pen_h));
    r->register_handler(std::move(sel_h));

    r->set_active(ToolKind::pen);
    r->set_active(ToolKind::select);
    EXPECT_EQ(pen_raw->deactivate_count, 1);  // pen got deactivated
    EXPECT_EQ(sel_raw->deactivate_count, 0);  // sel is now active

    r->set_active(ToolKind::pen);
    EXPECT_EQ(sel_raw->deactivate_count, 1);  // sel deactivated on the way back
    EXPECT_EQ(pen_raw->deactivate_count, 1);  // pen still 1 (newly activated, no second deactivate)
}

TEST(ToolInputRouter, SettingSameActiveIsNoOp) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    r->set_active(ToolKind::select);
    r->set_active(ToolKind::select);  // no-op
    r->set_active(ToolKind::select);  // no-op
    EXPECT_EQ(raw->deactivate_count, 0);
}

TEST(ToolInputRouter, SwitchToUnregisteredKindDeactivatesWithoutNewActive) {
    auto r = make_router();
    auto handler = std::make_unique<MockHandler>(ToolKind::select);
    auto* raw = handler.get();
    r->register_handler(std::move(handler));
    r->set_active(ToolKind::select);
    r->set_active(ToolKind::shape);  // no handler registered for shape
    EXPECT_EQ(raw->deactivate_count, 1);
    EXPECT_EQ(r->active(), nullptr);
}
