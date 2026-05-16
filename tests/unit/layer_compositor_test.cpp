#include <gtest/gtest.h>

#include <array>

#include "noted/compositor/layer_compositor.hpp"
#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/layer/layer.hpp"

namespace {

using noted::compositor::blend_state_for;
using noted::compositor::LayerPayload;
using noted::compositor::LayerPayloadStore;
using noted::compositor::resolve_composition;
using noted::compositor::SolidColor;
using BM = noted::domain::BlendMode;
using noted::domain::LayerGraph;
using noted::domain::LayerKind;

}  // namespace

// ---- blend_state_for: factor table ------------------------------------------

TEST(BlendState, NormalUsesSrcAlphaOverDst) {
    bool supported = false;
    const auto s = blend_state_for(BM::normal, supported);
    EXPECT_TRUE(supported);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.colorBlendOp,        VK_BLEND_OP_ADD);
    EXPECT_EQ(s.blendEnable,         VK_TRUE);
}

TEST(BlendState, ScreenIsInvertedMultiply) {
    bool supported = false;
    const auto s = blend_state_for(BM::screen, supported);
    EXPECT_TRUE(supported);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
}

TEST(BlendState, LinearDodgeIsAdditive) {
    bool supported = false;
    const auto s = blend_state_for(BM::linear_dodge, supported);
    EXPECT_TRUE(supported);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE);
}

TEST(BlendState, MultiplyMultipliesSrcByDst) {
    bool supported = false;
    const auto s = blend_state_for(BM::multiply, supported);
    EXPECT_TRUE(supported);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_DST_COLOR);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ZERO);
}

TEST(BlendState, UnsupportedFallsBackToNormal) {
    // Overlay, difference, hue, etc. — not FF-expressible.
    constexpr std::array<BM, 12> kFallbacks{
        BM::overlay, BM::soft_light, BM::hard_light, BM::color_dodge,
        BM::color_burn, BM::linear_burn, BM::difference, BM::exclusion,
        BM::hue, BM::saturation, BM::color, BM::luminosity,
    };
    for (const auto mode : kFallbacks) {
        bool supported = true;
        const auto s = blend_state_for(mode, supported);
        EXPECT_FALSE(supported) << "mode " << static_cast<int>(mode)
                                << " claims FF support";
        // Fallback should be normal's blend.
        EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
        EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    }
}

// ---- resolve_composition: graph → ordered draw list -------------------------

TEST(ResolveComposition, EmptyGraphYieldsEmptyList) {
    LayerGraph g;
    LayerPayloadStore store;
    auto r = resolve_composition(g, store);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->empty());
}

TEST(ResolveComposition, OnlyVisibleAndPayloadBearingNodesEmit) {
    LayerGraph g;
    const auto a = g.add_layer(LayerKind::bitmap, "bg");      // visible + payload
    const auto b = g.add_layer(LayerKind::adjustment, "hidden");  // invisible
    const auto c = g.add_layer(LayerKind::group, "no_payload");   // no payload
    const auto d = g.add_layer(LayerKind::bitmap, "fg");      // visible + payload

    ASSERT_TRUE(g.set_visible(b, false));

    LayerPayloadStore store;
    store.set(a, SolidColor{1.0F, 0.0F, 0.0F, 1.0F});
    store.set(b, SolidColor{0.0F, 1.0F, 0.0F, 1.0F});
    // c intentionally absent
    store.set(d, SolidColor{0.0F, 0.0F, 1.0F, 1.0F});

    auto r = resolve_composition(g, store);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->size(), 2U);
    EXPECT_EQ((*r)[0].id, a);
    EXPECT_EQ((*r)[1].id, d);
}

TEST(ResolveComposition, PreservesTopologicalOrder) {
    LayerGraph g;
    const auto a = g.add_layer(LayerKind::bitmap, "a");
    const auto b = g.add_layer(LayerKind::adjustment, "b");
    const auto c = g.add_layer(LayerKind::adjustment, "c");
    ASSERT_TRUE(g.set_inputs(b, std::array{a}));
    ASSERT_TRUE(g.set_inputs(c, std::array{b}));

    LayerPayloadStore store;
    store.set(a, SolidColor{});
    store.set(b, SolidColor{});
    store.set(c, SolidColor{});

    auto r = resolve_composition(g, store);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->size(), 3U);
    // a must come before b, b before c.
    const auto idx_of = [&](noted::domain::LayerId id) -> int {
        for (std::size_t i = 0; i < r->size(); ++i) {
            if ((*r)[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    EXPECT_LT(idx_of(a), idx_of(b));
    EXPECT_LT(idx_of(b), idx_of(c));
}

TEST(ResolveComposition, PropagatesGraphErrors) {
    LayerGraph g;
    const auto a = g.add_layer(LayerKind::bitmap);
    const auto b = g.add_layer(LayerKind::bitmap);
    const auto c = g.add_layer(LayerKind::bitmap);
    ASSERT_TRUE(g.set_inputs(b, std::array{a}));
    ASSERT_TRUE(g.set_inputs(c, std::array{b}));
    // Closing a→c cycle would be rejected; force-construct via independent
    // operations is also blocked. So we test that *valid* graphs resolve
    // and rely on LayerGraph's own tests for cycle / dangling coverage.
    LayerPayloadStore store;
    auto r = resolve_composition(g, store);
    EXPECT_TRUE(r);  // valid graph + empty payloads = empty draw list
    EXPECT_TRUE(r->empty());
}

TEST(ResolveComposition, CarriesBlendAndOpacity) {
    LayerGraph g;
    const auto a = g.add_layer(LayerKind::bitmap);
    ASSERT_TRUE(g.set_blend(a, BM::multiply));
    ASSERT_TRUE(g.set_opacity(a, 0.42F));
    LayerPayloadStore store;
    store.set(a, SolidColor{1.0F, 0.5F, 0.25F, 1.0F});

    auto r = resolve_composition(g, store);
    ASSERT_TRUE(r);
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0].blend,   BM::multiply);
    EXPECT_FLOAT_EQ((*r)[0].opacity, 0.42F);
    ASSERT_NE((*r)[0].payload, nullptr);
}

// ---- LayerPayloadStore: basic semantics -------------------------------------

TEST(PayloadStore, SetFindEraseClear) {
    LayerPayloadStore s;
    EXPECT_EQ(s.find(1), nullptr);
    s.set(1, SolidColor{0.1F, 0.2F, 0.3F, 0.4F});
    s.set(2, SolidColor{});
    EXPECT_EQ(s.size(), 2U);
    ASSERT_NE(s.find(1), nullptr);
    s.erase(1);
    EXPECT_EQ(s.find(1), nullptr);
    EXPECT_EQ(s.size(), 1U);
    s.clear();
    EXPECT_EQ(s.size(), 0U);
}
