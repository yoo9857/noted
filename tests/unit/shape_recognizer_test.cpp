#include "input/shape_recognizer.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"

namespace {

using noted::app::input::ShapeRecognitionAction;
using noted::app::input::ShapeRecognizer;
using noted::app::input::ShapeRecognizerConfig;
using noted::domain::Document;
using noted::domain::tool::ShapeKind;
using noted::stroke::BrushStyle;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;

[[nodiscard]] auto add_circle_stroke(Document& doc) -> std::size_t {
    Stroke s;
    s.style = BrushStyle{};
    constexpr int N = 48;
    constexpr float cx = 100.0F;
    constexpr float cy = 100.0F;
    constexpr float r = 60.0F;
    s.samples.reserve(N);
    for (int i = 0; i < N; ++i) {
        const float t =
            (static_cast<float>(i) / static_cast<float>(N)) * 2.0F * std::numbers::pi_v<float>;
        s.samples.push_back(
            {.x = cx + r * std::cos(t), .y = cy + r * std::sin(t), .pressure = 1.0F});
    }
    return *doc.add_stroke(std::move(s));
}

[[nodiscard]] auto enabled_config() -> ShapeRecognizerConfig {
    ShapeRecognizerConfig cfg{};
    cfg.enabled = true;
    cfg.hold_seconds = 0.5;
    return cfg;
}

}  // namespace

// ---- Off by default --------------------------------------------------------

TEST(ShapeRecognizer, DisabledByDefaultReturnsNullopt) {
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;  // default-constructed cfg.enabled = false
    rec.on_stroke_added(idx, /*now=*/0.0);
    EXPECT_FALSE(rec.tick(doc, /*now=*/10.0));  // huge wait still produces nothing
}

// ---- Hold-still threshold --------------------------------------------------

TEST(ShapeRecognizer, ReturnsNulloptBeforeHoldElapses) {
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 1.0);
    EXPECT_FALSE(rec.tick(doc, 1.2));  // 0.2 s elapsed, hold is 0.5
}

TEST(ShapeRecognizer, FiresAfterHoldElapsed) {
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 1.0);
    auto action = rec.tick(doc, 1.6);  // 0.6 s elapsed
    ASSERT_TRUE(action);
    EXPECT_EQ(action->stroke_index, idx);
    EXPECT_EQ(action->shape.kind, ShapeKind::ellipse);
}

// ---- Pointer activity cancels pending --------------------------------------

TEST(ShapeRecognizer, PointerActivityCancelsPending) {
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 1.0);
    rec.on_pointer_active(1.2);  // user moved the pen — abort
    EXPECT_FALSE(rec.tick(doc, 5.0));
}

// ---- Stroke removed before tick (e.g. user pressed Ctrl+Z) -----------------

TEST(ShapeRecognizer, DropsPendingWhenStrokeNoLongerExists) {
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 1.0);
    ASSERT_TRUE(doc.remove_stroke(idx));  // undo / external mutation
    EXPECT_FALSE(rec.tick(doc, 2.0));
}

// ---- Single-shot semantics -------------------------------------------------

TEST(ShapeRecognizer, ConsumesPendingAfterDetectionAttempt) {
    // Both successful and failed detection mark the stroke as
    // consumed so it doesn't re-fire every frame.
    Document doc;
    const auto idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 1.0);
    auto first = rec.tick(doc, 1.6);
    ASSERT_TRUE(first);
    // Stroke is still in the document — but the recognizer has
    // already given its verdict.
    EXPECT_FALSE(rec.tick(doc, 2.0));
    EXPECT_FALSE(rec.tick(doc, 10.0));
}

TEST(ShapeRecognizer, NewStrokeReArmsRecognition) {
    Document doc;
    const auto first_idx = add_circle_stroke(doc);
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(first_idx, 1.0);
    (void) rec.tick(doc, 1.6);  // consume first

    const auto second_idx = add_circle_stroke(doc);
    rec.on_stroke_added(second_idx, 2.0);
    auto a = rec.tick(doc, 2.6);
    ASSERT_TRUE(a);
    EXPECT_EQ(a->stroke_index, second_idx);
}

// ---- Failed detection still consumes ---------------------------------------

TEST(ShapeRecognizer, NoMatchReturnsNulloptAndDoesNotReFire) {
    // A wild scribble that detect_shape won't accept under default
    // tolerances.
    Document doc;
    Stroke s;
    s.style = BrushStyle{};
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 30.0F, .y = 50.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 200.0F, .pressure = 1.0F},
        {.x = 100.0F, .y = 50.0F, .pressure = 1.0F},
        {.x = 60.0F, .y = 150.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 80.0F, .pressure = 1.0F},
        {.x = 90.0F, .y = 10.0F, .pressure = 1.0F},
        {.x = 200.0F, .y = 200.0F, .pressure = 1.0F},
    };
    const auto idx = *doc.add_stroke(std::move(s));
    ShapeRecognizer rec;
    rec.set_config(enabled_config());
    rec.on_stroke_added(idx, 0.0);
    EXPECT_FALSE(rec.tick(doc, 1.0));  // no match
    EXPECT_FALSE(rec.tick(doc, 5.0));  // still no re-fire
}
