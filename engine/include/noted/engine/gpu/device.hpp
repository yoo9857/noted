#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/surface.hpp"

namespace noted::gpu {

struct DeviceCreateInfo {
    // Caller-provided extensions are appended to the engine-required
    // baseline (VK_KHR_swapchain).
    std::span<const char* const> extra_extensions = {};

    // Vulkan 1.3 features the engine assumes. Backends/tests can disable
    // these temporarily, but the renderer relies on dynamic_rendering and
    // synchronization2 across the codebase.
    bool enable_dynamic_rendering = true;
    bool enable_synchronization2 = true;

    // ---- Vulkan 1.2 modern feature set ----
    // The engine treats these as the "2026 baseline" — every backend assumes
    // they are available. The Device::create call checks support and returns
    // an error if the picked physical device cannot honor them.
    //
    // descriptor_indexing               — runtime descriptor arrays, partial
    //                                     binding, update-after-bind. The
    //                                     foundation for bindless rendering.
    // buffer_device_address              — pointer-as-uniform GPU access; used
    //                                     by ray tracing and modern shader code.
    // timeline_semaphore                 — single primitive replaces fences +
    //                                     binary semaphores for cross-queue sync.
    bool enable_descriptor_indexing = true;
    bool enable_buffer_device_address = true;
    bool enable_timeline_semaphore = true;

    // ---- Vulkan 1.1 features the engine relies on ----
    // shader_draw_parameters     — exposes SV_VertexID / SV_InstanceID
    //                              (gl_VertexIndex / gl_InstanceIndex) to
    //                              shaders. Required by every vertex shader
    //                              that draws without a vertex buffer
    //                              (fullscreen quad, instanced sprites).
    bool enable_shader_draw_parameters = true;

    // ---- Vulkan 1.4 (optional) ----
    // dynamic_rendering_local_read — fragment shader can read the
    // current color attachment value via input-attachment-style
    // semantics under dynamic rendering. Required by the layer
    // compositor's shader-blend pipeline for the 12 Photoshop blend
    // modes that aren't expressible via fixed-function blend (overlay,
    // soft_light, hue, …). Opportunistic: when the physical device
    // doesn't support the feature, Device::create() leaves it
    // disabled and the compositor falls back to NORMAL for those
    // modes (the same behaviour the pre-Phase-C code had).
    bool enable_dynamic_rendering_local_read = true;
};

// Logical device + queue handles.
//
// Picks queues with these rules:
//   - graphics_queue is the queue from the graphics family.
//   - present_queue is the queue from the *first* family that supports
//     presenting to the given Surface. If the graphics family already
//     supports present (the common case), the two handles point at the
//     same VkQueue.
//
// Move-only RAII. vkDestroyDevice is called from the destructor.
class Device {
public:
    [[nodiscard]] static auto create(const PhysicalDevice& physical,
                                     const Surface& surface,
                                     const DeviceCreateInfo& info = {}) -> Result<Device>;

    Device(Device&& other) noexcept;
    auto operator=(Device&& other) noexcept -> Device&;
    Device(const Device&) = delete;
    auto operator=(const Device&) -> Device& = delete;
    ~Device();

    [[nodiscard]] auto handle() const noexcept -> VkDevice { return handle_; }
    [[nodiscard]] auto graphics_queue() const noexcept -> VkQueue { return graphics_queue_; }
    [[nodiscard]] auto present_queue() const noexcept -> VkQueue { return present_queue_; }
    [[nodiscard]] auto graphics_family() const noexcept -> std::uint32_t {
        return graphics_family_;
    }
    [[nodiscard]] auto present_family() const noexcept -> std::uint32_t { return present_family_; }

    // True iff Device::create successfully enabled the Vulkan 1.4
    // `dynamicRenderingLocalRead` feature. Callers that want to use
    // a shader pipeline reading its own color attachment must check
    // this before creating that pipeline. False is the same code path
    // the engine took before Phase C: shader-blend pipelines are
    // skipped and the compositor falls back to NORMAL for the modes
    // that would have used them.
    [[nodiscard]] auto has_dynamic_rendering_local_read() const noexcept -> bool {
        return has_dynamic_rendering_local_read_;
    }

    // Blocks the calling thread until every queue on this device is idle.
    // Used before destroying resources that the GPU might still reference.
    void wait_idle() const noexcept;

private:
    Device() = default;
    void destroy() noexcept;

    VkDevice handle_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = UINT32_MAX;
    std::uint32_t present_family_ = UINT32_MAX;
    bool has_dynamic_rendering_local_read_ = false;
};

}  // namespace noted::gpu
