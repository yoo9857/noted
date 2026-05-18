#pragma once

// ImGuiHost — RAII lifecycle wrapper around Dear ImGui's Vulkan +
// GLFW backends.
//
// Sequence (one per process):
//
//     auto host = ui::ImGuiHost::create({ ...vulkan + window... });
//     while (running) {
//         host->begin_frame();
//         ImGui::ShowDemoWindow();           // user UI code
//         host->end_frame(command_buffer);   // record draw into the cb
//     }
//     // host's destructor runs ImGui_ImplVulkan_Shutdown +
//     // ImGui_ImplGlfw_Shutdown + ImGui::DestroyContext.
//
// Failure policy (ADR 0003):
//   - create() returns Result<ImGuiHost>; rejection paths cover
//     null device / queue / window pointers and Vulkan resource
//     creation failures. No exceptions propagate.
//   - begin_frame / end_frame are noexcept. ImGui's own
//     assertions (IM_ASSERT) currently route through the standard
//     `assert()` — routing through harness::validate is deferred
//     to a small follow-up PR (see ADR 0027).
//
// Threading:
//   - Dear ImGui's global context model means only one ImGuiHost
//     may exist at a time per process. The class is move-only and
//     non-default-constructible to enforce this with type-level
//     ergonomics rather than runtime checks.
//
// Rationale: see docs/architecture/0027-ui-stack-selection.md.

#include <cstdint>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"

struct GLFWwindow;

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
    GLFWwindow* window{nullptr};

    // Image format the backend will write to via dynamic rendering.
    // Typically `Swapchain::summary().color_format`.
    VkFormat color_format{VK_FORMAT_UNDEFINED};

    // Swapchain image count. Must match the surrounding renderer's
    // frame-in-flight count for ImGui's per-frame state.
    std::uint32_t image_count{2};

    // ImGui's font atlas size is bounded by VRAM; the default is
    // sufficient for ASCII + Latin Extended. CJK glyph ranges are
    // wired in the theme-pass PR.
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
    // ImGui::* drawing calls. noexcept: ImGui's frame setup never
    // throws under the supported config.
    void begin_frame() noexcept;

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
    bool glfw_init_{false};
    bool vulkan_init_{false};
    bool initialized_{false};
};

}  // namespace noted::ui
