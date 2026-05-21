#pragma once

// ImGuiHost — RAII lifecycle wrapper around Dear ImGui's Vulkan
// backend + the platform-agnostic input bridge.
//
// Sequence (one per process):
//
//     auto host = ui::ImGuiHost::create({ ...vulkan only... });
//     while (running) {
//         host->begin_frame(fb_w, fb_h, dt);
//         ImGui::ShowDemoWindow();           // user UI code
//         host->finalize_frame();
//         host->render_into(command_buffer); // record draw into the cb
//     }
//     // host's destructor runs ImGui_ImplVulkan_Shutdown + bridge
//     // teardown + ImGui::DestroyContext.
//
// Phase 1 of ADR 0034: replaced the GLFW backend
// (`imgui_impl_glfw`) with `ui::ImGuiInputBridge`, which feeds ImGui
// IO from the engine's hook registry. ImGui no longer cares which
// platform window framework hosts it.
//
// Failure policy (ADR 0003):
//   - create() returns Result<ImGuiHost>; rejection paths cover
//     null device pointers and Vulkan resource creation failures.
//     No exceptions propagate.
//   - begin_frame / finalize_frame / render_into are noexcept.
//
// Threading:
//   - Dear ImGui's global context model means only one ImGuiHost
//     may exist at a time per process. The class is move-only and
//     non-default-constructible to enforce this with type-level
//     ergonomics rather than runtime checks.
//
// Rationale: see docs/architecture/0027-ui-stack-selection.md
// and docs/architecture/0034-ui-framework-migration-qt6.md.

#include <cstdint>
#include <filesystem>
#include <memory>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/ui/imgui_input_bridge.hpp"

namespace noted::gpu {
class Device;
class Instance;
class PhysicalDevice;
}  // namespace noted::gpu

namespace noted::ui {

struct ImGuiHostCreateInfo {
    const noted::gpu::Instance* instance{nullptr};
    const noted::gpu::PhysicalDevice* physical_device{nullptr};
    const noted::gpu::Device* device{nullptr};

    // Image format the backend will write to via dynamic rendering.
    // Typically `Swapchain::summary().color_format`.
    VkFormat color_format{VK_FORMAT_UNDEFINED};

    // Swapchain image count. Must match the surrounding renderer's
    // frame-in-flight count for ImGui's per-frame state.
    std::uint32_t image_count{2};

    // Optional symbol-rich fallback font. Merged into the ImGui
    // atlas after the primary CJK face so missing glyphs (icon
    // characters from the dingbat / geometric / arrows blocks) get
    // rendered by the fallback instead of as the tofu box. On
    // Windows this is typically Segoe UI Symbol; on macOS Apple
    // Symbols; on Linux DejaVu Sans. Empty path skips the merge.
    std::filesystem::path symbol_font_path{};

    // Optional CJK-capable TrueType / OpenType font. When set and the
    // file exists, ImGuiHost loads it as the primary font with the
    // Korean glyph range merged in (Hangul Syllables + Jamo + Latin
    // basic). When empty, missing, or unreadable, ImGui's default
    // ProggyClean bitmap font is used and a warning is written to
    // stderr — the host never fails create() over a font issue,
    // because a missing font is a degraded-but-usable state, not a
    // fatal error in line with ADR 0027's resilience posture.
    std::filesystem::path cjk_font_path{};

    // Pixel size of the loaded font. Ignored when cjk_font_path is
    // empty (ProggyClean is bitmap-fixed at ~13 px). 16 is the
    // smallest size that keeps Hangul legible without subpixel
    // hinting; 18-20 is more comfortable on high-DPI displays.
    float font_size_px{16.0F};
};

class ImGuiHost {
public:
    [[nodiscard]] static auto create(const ImGuiHostCreateInfo& info) -> Result<ImGuiHost>;

    ImGuiHost(ImGuiHost&& other) noexcept;
    auto operator=(ImGuiHost&& other) noexcept -> ImGuiHost&;
    ImGuiHost(const ImGuiHost&) = delete;
    auto operator=(const ImGuiHost&) -> ImGuiHost& = delete;
    ~ImGuiHost();

    // Top of frame — must be called once per frame, before any
    // ImGui::* drawing calls. `framebuffer_w_px` / `framebuffer_h_px`
    // are the current swapchain image extent (in pixels — already
    // HiDPI-scaled). `delta_seconds` is the time since the previous
    // call; the host passes its frame-loop's measured Δ.
    void begin_frame(float framebuffer_w_px, float framebuffer_h_px, float delta_seconds) noexcept;

    // Finalize the frame's ImGui state and produce internal draw
    // data. Must be called every frame regardless of whether
    // render_into will run — otherwise ImGui leaves an unfinished
    // frame and the next begin_frame trips an assertion.
    void finalize_frame() noexcept;

    // Record the finalized draw data into `cb`. The caller is
    // responsible for the surrounding `vkCmdBeginRendering` /
    // `vkCmdEndRendering` pair whose color attachment is the
    // swapchain image at `COLOR_ATTACHMENT_OPTIMAL`. Safe to skip
    // on frames where rendering was aborted — the frame is already
    // finalized.
    void render_into(VkCommandBuffer cb) noexcept;

    [[nodiscard]] auto initialized() const noexcept -> bool { return initialized_; }

private:
    ImGuiHost() = default;
    void destroy() noexcept;

    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
    bool context_owned_{false};
    bool vulkan_init_{false};
    bool initialized_{false};
    std::unique_ptr<ImGuiInputBridge> input_bridge_{};
};

}  // namespace noted::ui
