#include <string>

#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/io/document_json.hpp"

namespace {

using noted::domain::Document;
using noted::domain::io::document_from_json;
using noted::domain::io::document_to_json;
using noted::domain::io::kDocumentJsonVersion;
using noted::stroke::DrawMode;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;

[[nodiscard]] auto make_stroke() -> Stroke {
    Stroke s{};
    s.samples = {{.x = 1.0F, .y = 2.0F, .pressure = 0.8F},
                 {.x = 3.0F, .y = 4.0F, .pressure = 0.5F}};
    s.style.min_radius_px = 2.0F;
    s.style.max_radius_px = 8.0F;
    s.style.alpha_gamma = 1.5F;
    s.style.r = 0.1F;
    s.style.g = 0.2F;
    s.style.b = 0.3F;
    s.style.a = 0.9F;
    s.style.stabilizer = 0.4F;
    s.mode = DrawMode::draw;
    return s;
}

TEST(DocumentCanvasLayersJson, RoundTripPreservesLayerIdAndStack) {
    Document src;
    auto a = *src.add_canvas_layer("Background");
    auto b = *src.add_canvas_layer("Foreground");
    ASSERT_TRUE(src.set_active_layer(b));
    // Hide layer A to exercise the visible field on disk.
    ASSERT_TRUE(src.set_layer_visible(a, false));
    // First stroke goes on active (b); second pinned to a manually.
    (void) *src.add_stroke(make_stroke());
    Stroke s2 = make_stroke();
    s2.layer_id = a;
    (void) *src.add_stroke(s2);

    const auto text = document_to_json(src);
    auto loaded = document_from_json(text);
    ASSERT_TRUE(loaded);

    EXPECT_EQ(loaded->canvas_layers().size(), 2U);
    ASSERT_NE(loaded->canvas_layers().find(a), nullptr);
    EXPECT_EQ(loaded->canvas_layers().find(a)->name, "Background");
    EXPECT_FALSE(loaded->canvas_layers().find(a)->visible);
    EXPECT_EQ(loaded->canvas_layers().find(b)->name, "Foreground");
    EXPECT_EQ(loaded->active_layer(), b);

    ASSERT_EQ(loaded->strokes().size(), 2U);
    EXPECT_EQ(loaded->strokes()[0].layer_id, b);
    EXPECT_EQ(loaded->strokes()[1].layer_id, a);
}

TEST(DocumentCanvasLayersJson, V7DocumentMigratesToDefaultLayer) {
    // Hand-build a minimal v7 document with one stroke and no
    // canvas_layers block. The loader must synthesize "Layer 1" and
    // pin the stroke's layer_id to it.
    const std::string v7 = R"({
        "version": 7,
        "root": 0,
        "blocks": [],
        "strokes": [
            {
                "mode": 0,
                "samples": [1.0, 2.0, 1.0, 3.0, 4.0, 1.0],
                "style": {
                    "min_r": 2.0, "max_r": 8.0, "soft": 0.2, "ag": 1.8,
                    "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0, "stab": 0.5
                }
            }
        ]
    })";
    auto loaded = document_from_json(v7);
    ASSERT_TRUE(loaded) << (loaded ? "" : loaded.error().format());

    ASSERT_EQ(loaded->canvas_layers().size(), 1U);
    EXPECT_EQ(loaded->canvas_layers().layers()[0].name, "Layer 1");
    const auto layer_id = loaded->canvas_layers().layers()[0].id;
    EXPECT_NE(layer_id, noted::invalid_layer_id);
    EXPECT_EQ(loaded->active_layer(), layer_id);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    EXPECT_EQ(loaded->strokes()[0].layer_id, layer_id);
}

TEST(DocumentCanvasLayersJson, V7EmptyStrokesProducesEmptyStack) {
    // v7 doc with NO strokes must NOT synthesize a layer — empty
    // doc stays empty, matching `Document{}` semantics.
    const std::string v7 = R"({
        "version": 7,
        "root": 0,
        "blocks": []
    })";
    auto loaded = document_from_json(v7);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->canvas_layers().empty());
    EXPECT_TRUE(loaded->strokes().empty());
    EXPECT_EQ(loaded->active_layer(), noted::invalid_layer_id);
}

TEST(DocumentCanvasLayersJson, OrphanLidLoadsAsIs) {
    // A v8 doc whose stroke references a layer NOT in the stack
    // should still load — the renderer treats orphan strokes as
    // hidden via the `is_visible` contract.
    const std::string v8 = R"({
        "version": 8,
        "root": 0,
        "blocks": [],
        "strokes": [
            {
                "mode": 0,
                "lid": 999,
                "samples": [0.0, 0.0, 1.0, 5.0, 0.0, 1.0],
                "style": {
                    "min_r": 2.0, "max_r": 8.0, "soft": 0.2, "ag": 1.8,
                    "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0, "stab": 0.5
                }
            }
        ],
        "canvas_layers": {
            "active": 1,
            "items": [
                {"id": 1, "name": "Real", "visible": true, "opacity": 1.0}
            ]
        }
    })";
    auto loaded = document_from_json(v8);
    ASSERT_TRUE(loaded) << (loaded ? "" : loaded.error().format());
    ASSERT_EQ(loaded->strokes().size(), 1U);
    EXPECT_EQ(loaded->strokes()[0].layer_id, 999U);
    EXPECT_FALSE(loaded->canvas_layers().is_visible(999));
}

TEST(DocumentCanvasLayersJson, UnknownTopLevelKeyRejects) {
    const std::string bad = R"({"version": 8, "root": 0, "blocks": [], "bogus": 1})";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentCanvasLayersJson, DuplicateLayerIdRejects) {
    const std::string bad = R"({
        "version": 8,
        "root": 0,
        "blocks": [],
        "canvas_layers": {
            "active": 0,
            "items": [
                {"id": 1, "name": "A", "visible": true, "opacity": 1.0},
                {"id": 1, "name": "B", "visible": true, "opacity": 1.0}
            ]
        }
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentCanvasLayersJson, ZeroLayerIdRejects) {
    const std::string bad = R"({
        "version": 8,
        "root": 0,
        "blocks": [],
        "canvas_layers": {
            "active": 0,
            "items": [
                {"id": 0, "name": "BadId", "visible": true, "opacity": 1.0}
            ]
        }
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentCanvasLayersJson, EmittedVersionIsCurrent) {
    Document src;
    const auto text = document_to_json(src);
    EXPECT_NE(text.find("\"version\": " + std::to_string(kDocumentJsonVersion)), std::string::npos);
}

}  // namespace
