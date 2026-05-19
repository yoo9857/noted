#include "noted/ui/widget/debug_overlay.hpp"

#include <algorithm>
#include <string>

#include <imgui.h>

#include "noted/engine/harness/harness.hpp"

namespace noted::ui::widget {

namespace {

// Linearize the ring buffer into a temporary array so ImGui's
// PlotLines can read it as a flat sequence. `state.head` points at
// the *next* slot to write, so the oldest valid sample is at
// `(head - size + capacity) % capacity` — wrap-aware.
//
// For typical sizes (≤120) the per-frame copy is irrelevant. Keep
// this branchless / simple over clever in-place tricks.
auto linearize_samples(const DebugOverlayState& state,
                       std::array<float, DebugOverlayState::kCapacity>& out) -> std::size_t {
    if (state.size == 0) {
        return 0;
    }
    const std::size_t cap = DebugOverlayState::kCapacity;
    const std::size_t start = (state.head + cap - state.size) % cap;
    for (std::size_t i = 0; i < state.size; ++i) {
        out[i] = state.samples[(start + i) % cap];
    }
    return state.size;
}

}  // namespace

void debug_overlay(const DebugOverlayInputs& inputs, const DebugOverlayState& state, bool* open) {
    if (open != nullptr && !*open) {
        return;
    }

    // Top-right corner by default, opaque enough to read over the
    // composite. SetNextWindowPos only takes effect on Appearing so
    // the user can drag and re-dock.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport != nullptr) {
        const ImVec2 pos{viewport->WorkPos.x + viewport->WorkSize.x - 16.0F,
                         viewport->WorkPos.y + 16.0F};
        ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowSize(ImVec2(300.0F, 0.0F), ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowBgAlpha(0.85F);

    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Debug overlay", open, kFlags)) {
        ImGui::End();
        return;
    }

    // ---- Frame / FPS row -------------------------------------------------
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("frame %llu", static_cast<unsigned long long>(inputs.frame_index));
    ImGui::Text("%.1f FPS  (%.2f ms avg)", io.Framerate, 1000.0F / io.Framerate);

    // ---- CPU-time plot ---------------------------------------------------
    ImGui::Separator();
    std::array<float, DebugOverlayState::kCapacity> flat{};
    const std::size_t count = linearize_samples(state, flat);
    float min_v = 0.0F;
    float max_v = 0.0F;
    float last_v = 0.0F;
    if (count > 0) {
        min_v = *std::min_element(flat.begin(), flat.begin() + static_cast<std::ptrdiff_t>(count));
        max_v = *std::max_element(flat.begin(), flat.begin() + static_cast<std::ptrdiff_t>(count));
        last_v = flat[count - 1];
    }
    // Scale floor at 0 ms; ceiling pads 1 ms above the observed max
    // so a flat 0.5 ms trace doesn't render as a centered line. The
    // 16 ms cap mirrors a 60 Hz frame budget for visual reference.
    const float scale_max = std::max(max_v + 1.0F, 16.0F);
    char overlay_label[64];
    std::snprintf(overlay_label,
                  sizeof(overlay_label),
                  "cpu ms  cur %.2f  min %.2f  max %.2f",
                  static_cast<double>(last_v),
                  static_cast<double>(min_v),
                  static_cast<double>(max_v));
    ImGui::PlotLines("##cpu_plot",
                     flat.data(),
                     static_cast<int>(count),
                     /*values_offset=*/0,
                     overlay_label,
                     0.0F,
                     scale_max,
                     ImVec2(0.0F, 60.0F));

    // ---- LayerCompositor fallback count ----------------------------------
    ImGui::Separator();
    ImGui::Text("compositor fallback: %llu",
                static_cast<unsigned long long>(inputs.fallback_count));
    if (inputs.fallback_count > 0) {
        ImGui::TextDisabled("(blend modes without a fixed-function pipeline drew via NORMAL)");
    }

    // ---- Canvas camera --------------------------------------------------
    if (inputs.camera_scale > 0.0) {
        ImGui::Separator();
        ImGui::TextUnformatted("camera");
        ImGui::Text("zoom   %.0f%%", inputs.camera_scale * 100.0);
        ImGui::Text("pan    %.0f, %.0f", inputs.camera_translation_x, inputs.camera_translation_y);
    }

    // ---- Harness counters ------------------------------------------------
    // `all_counters()` returns the live set registered at static-init
    // time — typically a handful of engine-lifecycle counters. The
    // table is small enough that re-querying per frame is irrelevant.
    ImGui::Separator();
    ImGui::TextUnformatted("harness counters");
    if (ImGui::BeginTable(
            "##counters", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 90.0F);
        for (const auto* c : noted::harness::all_counters()) {
            if (c == nullptr) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(c->name().data(), c->name().data() + c->name().size());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(c->value()));
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace noted::ui::widget
