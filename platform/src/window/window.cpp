#include "noted/platform/window/window.hpp"

#include <GLFW/glfw3.h>

#include <atomic>
#include <mutex>
#include <string>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::platform {

namespace {

[[nodiscard]] auto map_button(int glfw_button) -> noted::hook::PointerButton {
    switch (glfw_button) {
        case GLFW_MOUSE_BUTTON_LEFT:   return noted::hook::PointerButton::left;
        case GLFW_MOUSE_BUTTON_RIGHT:  return noted::hook::PointerButton::right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return noted::hook::PointerButton::middle;
        default:                       return noted::hook::PointerButton::other;
    }
}

// Convert window-space coords (where GLFW reports cursor) to framebuffer-space
// coords. On HiDPI displays the two differ by the content scale.
[[nodiscard]] auto window_to_framebuffer(GLFWwindow* w, double xw, double yw)
    -> std::pair<double, double> {
    int ww = 0;
    int wh = 0;
    int fw = 0;
    int fh = 0;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetFramebufferSize(w, &fw, &fh);
    if (ww <= 0 || wh <= 0) {
        return {xw, yw};
    }
    const double sx = static_cast<double>(fw) / static_cast<double>(ww);
    const double sy = static_cast<double>(fh) / static_cast<double>(wh);
    return {xw * sx, yw * sy};
}

void on_cursor_pos(GLFWwindow* w, double xw, double yw) {
    const auto [x, y] = window_to_framebuffer(w, xw, yw);
    noted::hook::registry().on_pointer_moved.publish(noted::hook::PointerMoved{
        .x = x, .y = y,
    });
}

void on_mouse_button(GLFWwindow* w, int button, int action, int /*mods*/) {
    double xw = 0.0;
    double yw = 0.0;
    glfwGetCursorPos(w, &xw, &yw);
    const auto [x, y] = window_to_framebuffer(w, xw, yw);
    if (action == GLFW_PRESS) {
        noted::hook::registry().on_pointer_pressed.publish(noted::hook::PointerPressed{
            .x = x, .y = y,
            .button = map_button(button),
        });
    } else if (action == GLFW_RELEASE) {
        noted::hook::registry().on_pointer_released.publish(noted::hook::PointerReleased{
            .x = x, .y = y,
            .button = map_button(button),
        });
    }
}

void on_scroll(GLFWwindow* /*w*/, double dx, double dy) {
    noted::hook::registry().on_scrolled.publish(noted::hook::Scrolled{.dx = dx, .dy = dy});
}

void on_key(GLFWwindow* /*w*/, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        noted::hook::registry().on_key_pressed.publish(noted::hook::KeyPressed{
            .glfw_key  = key,
            .scancode  = scancode,
            .mods      = mods,
            .is_repeat = (action == GLFW_REPEAT),
        });
    } else if (action == GLFW_RELEASE) {
        noted::hook::registry().on_key_released.publish(noted::hook::KeyReleased{
            .glfw_key = key,
            .scancode = scancode,
            .mods     = mods,
        });
    }
}

void on_framebuffer_size(GLFWwindow* /*w*/, int width, int height) {
    noted::hook::registry().on_framebuffer_resized.publish(noted::hook::FramebufferResized{
        .width  = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    });
}

}  // namespace

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

    // Wire input callbacks. Events are published to the global hook registry;
    // listeners subscribe there. We do not stash a per-window user pointer
    // because the registry is process-global today (see ADR 0002).
    glfwSetCursorPosCallback(h,       &on_cursor_pos);
    glfwSetMouseButtonCallback(h,     &on_mouse_button);
    glfwSetScrollCallback(h,          &on_scroll);
    glfwSetKeyCallback(h,             &on_key);
    glfwSetFramebufferSizeCallback(h, &on_framebuffer_size);

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
