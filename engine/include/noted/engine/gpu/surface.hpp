#pragma once

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/instance.hpp"

namespace noted::gpu {

// RAII wrapper around VkSurfaceKHR.
//
// The platform-specific creation call (e.g. glfwCreateWindowSurface) is
// performed by the caller — keeping this header free of any windowing
// dependency. The caller then hands the raw handle off via adopt(); from
// that point Surface owns destruction.
//
// Example:
//
//     VkSurfaceKHR raw = VK_NULL_HANDLE;
//     if (glfwCreateWindowSurface(inst.handle(), win.native_handle(),
//                                 nullptr, &raw) != VK_SUCCESS) { ... }
//     auto surface = noted::gpu::Surface::adopt(inst, raw);
class Surface {
public:
    [[nodiscard]] static auto adopt(const Instance& inst, VkSurfaceKHR raw) noexcept -> Surface;

    Surface(Surface&& other) noexcept;
    auto operator=(Surface&& other) noexcept -> Surface&;
    Surface(const Surface&) = delete;
    auto operator=(const Surface&) -> Surface& = delete;
    ~Surface();

    [[nodiscard]] auto handle() const noexcept -> VkSurfaceKHR { return handle_; }
    [[nodiscard]] auto instance() const noexcept -> VkInstance { return owner_; }

private:
    Surface() = default;
    void destroy() noexcept;

    VkInstance owner_ = VK_NULL_HANDLE;
    VkSurfaceKHR handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
