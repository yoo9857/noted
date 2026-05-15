#include "noted/platform/window/window.hpp"

#include <GLFW/glfw3.h>

#include <atomic>
#include <mutex>
#include <string>

#include "noted/engine/hook/registry.hpp"

namespace noted::platform {

namespace {

// GLFW is global state. We initialize on first use and terminate on last
// release; tests and the app share the same loader.
std::mutex             g_glfw_mu;
std::atomic<int>       g_glfw_refs{0};

[[nodiscard]] auto ensure_glfw_init() -> noted::Result<void> {
    std::scoped_lock lk(g_glfw_mu);
    if (g_glfw_refs.load(std::memory_order_acquire) == 0) {
        if (glfwInit() != GLFW_TRUE) {
            const char* desc = nullptr;
            (void)glfwGetError(&desc);
            return std::unexpected(noted::make_error(
                noted::ErrorCode::invalid_state,
                std::string{"glfwInit failed: "} + (desc != nullptr ? desc : "unknown")));
        }
    }
    g_glfw_refs.fetch_add(1, std::memory_order_acq_rel);
    return {};
}

void release_glfw() noexcept {
    std::scoped_lock lk(g_glfw_mu);
    if (g_glfw_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        glfwTerminate();
    }
}

}  // namespace

auto Window::create(const WindowDesc& desc) -> Result<Window> {
    if (auto r = ensure_glfw_init(); !r) {
        return std::unexpected(std::move(r).error());
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // we'll render with Vulkan
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, desc.high_dpi ? GLFW_TRUE : GLFW_FALSE);

    const std::string title{desc.title};
    GLFWwindow* h = glfwCreateWindow(
        static_cast<int>(desc.width),
        static_cast<int>(desc.height),
        title.c_str(),
        nullptr, nullptr);

    if (h == nullptr) {
        const char* err = nullptr;
        (void)glfwGetError(&err);
        release_glfw();
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            std::string{"glfwCreateWindow failed: "} + (err != nullptr ? err : "unknown")));
    }
    return Window{h};
}

Window::Window(Window&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

auto Window::operator=(Window&& other) noexcept -> Window& {
    if (this != &other) {
        destroy();
        handle_       = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Window::~Window() { destroy(); }

void Window::destroy() noexcept {
    if (handle_ != nullptr) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        release_glfw();
    }
}

auto Window::should_close() const noexcept -> bool {
    return handle_ != nullptr && glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void Window::poll_events() noexcept {
    glfwPollEvents();
}

auto Window::framebuffer_size() const noexcept
    -> std::pair<std::uint32_t, std::uint32_t> {
    int w = 0;
    int h = 0;
    if (handle_ != nullptr) {
        glfwGetFramebufferSize(handle_, &w, &h);
    }
    return {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
}

}  // namespace noted::platform
