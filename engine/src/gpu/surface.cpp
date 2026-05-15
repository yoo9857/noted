#include "noted/engine/gpu/surface.hpp"

namespace noted::gpu {

auto Surface::adopt(const Instance& inst, VkSurfaceKHR raw) noexcept -> Surface {
    Surface s;
    s.owner_  = inst.handle();
    s.handle_ = raw;
    return s;
}

Surface::Surface(Surface&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_  = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto Surface::operator=(Surface&& other) noexcept -> Surface& {
    if (this != &other) {
        destroy();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

Surface::~Surface() { destroy(); }

void Surface::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_  = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
