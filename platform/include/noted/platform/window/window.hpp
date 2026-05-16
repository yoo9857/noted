#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "noted/engine/error/error.hpp"

// Forward-declare GLFW's window type so this header does not require
// <GLFW/glfw3.h>. Consumers that need the raw handle can cast the void*
// returned by native_handle(), or include the GLFW header themselves.
struct GLFWwindow;

namespace noted::platform {

struct WindowDesc {
    std::string_view title{"noted"};
    std::uint32_t width = 1600;
    std::uint32_t height = 1000;
    bool resizable = true;
    bool high_dpi = true;
};

// RAII move-only handle around a GLFW window.
//
// Construction goes through Window::create(WindowDesc) so failures surface
// as Result<Window>, never as a half-constructed object. There is no copy
// and no shared ownership — the platform layer owns the resource until the
// Window is destroyed.
//
// Threading: every method must be called from the thread that called
// Window::create (GLFW's main-thread invariant). poll_events drives the
// event pump; the engine main loop owns it.
class Window {
public:
    [[nodiscard]] static auto create(const WindowDesc& desc) -> Result<Window>;

    Window(Window&& other) noexcept;
    auto operator=(Window&& other) noexcept -> Window&;
    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    ~Window();

    [[nodiscard]] auto should_close() const noexcept -> bool;
    void poll_events() noexcept;

    [[nodiscard]] auto framebuffer_size() const noexcept -> std::pair<std::uint32_t, std::uint32_t>;

    // Raw handle for Vulkan surface creation. Stable for the lifetime of
    // this Window object.
    [[nodiscard]] auto native_handle() const noexcept -> GLFWwindow* { return handle_; }

private:
    explicit Window(GLFWwindow* h) noexcept : handle_(h) {}
    void destroy() noexcept;

    GLFWwindow* handle_ = nullptr;
};

}  // namespace noted::platform
