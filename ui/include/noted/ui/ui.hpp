#pragma once

// UI layer.
//
// v0.x implementation: **Dear ImGui** (docking branch) with the
// official Vulkan + GLFW backends. ImGui renders into our existing
// VkCommandBuffer alongside `LayerCompositor`; one window, one
// renderer, one event loop. See ADR 0027.
//
// The 4-area split below is the API contract that makes a future
// chrome swap (Qt 6 / Slint / custom) feasible without touching
// domain or engine:
//
//   view/     observes domain state, emits draw calls
//   widget/   composite primitives (LayerPanel, OutlineTree, …)
//   theme/    colors, fonts, spacing; pushes ImGuiStyle away from
//             its default "debug tool" look toward product chrome
//   binding/  hook-channel → view-state plumbing
//
// Exception / failure policy (ADR 0003): UI init/shutdown returns
// `Result<void>`. ImGui's `IM_ASSERT` is routed through
// `harness::validate` so internal invariants surface in the same
// channel as every other engine assertion. The hot path (per-frame
// draw) is noexcept; render failures degrade to a blank canvas
// rather than propagating up.

#include "noted/ui/binding/binding.hpp"
#include "noted/ui/theme/theme.hpp"
#include "noted/ui/view/view.hpp"
#include "noted/ui/widget/widget.hpp"

namespace noted::ui {}
