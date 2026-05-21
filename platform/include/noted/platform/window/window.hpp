#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"

namespace noted::platform {

struct WindowDesc {
    std::string_view title{"noted"};
    std::uint32_t width = 1600;
    std::uint32_t height = 1000;
    bool resizable = true;
    bool high_dpi = true;
    // **Phase 2 of ADR 0034.** When true the OS-native title bar +
    // window chrome are hidden and the app paints its own (Mac-style
    // traffic-light buttons, drag region, theme — see
    // `ui::widget::mac_chrome`). On Windows 11 the corners are
    // rounded automatically via DWM; on Windows 10 / Linux the window
    // is square-cornered but the custom chrome still renders.
    bool frameless = true;
};

// RAII move-only handle around a Qt-backed window.
//
// **Phase 1 of ADR 0034 (UI migration to Qt 6 + QML).** Internally
// wraps a `QWindow` with `QSurface::VulkanSurface`; surface creation
// goes through a `QVulkanInstance` that the Window owns and bridges
// from the host's existing `VkInstance`. Input events flow through
// `QMouseEvent` / `QKeyEvent` / `QWheelEvent` / `QResizeEvent` /
// `QCloseEvent` and re-publish on the global hook registry — the
// downstream subscribers (camera controller, stroke engine, tool
// router, ImGui input bridge) keep working unchanged.
//
// Construction goes through Window::create(WindowDesc) so failures
// surface as Result<Window>, never as a half-constructed object.
// There is no copy and no shared ownership — the platform layer
// owns the resource until the Window is destroyed.
//
// Threading: every method must be called from the thread that called
// Window::create (Qt's main-thread invariant for QWindow). The
// `poll_events` call drives `QCoreApplication::processEvents()`;
// callers continue to own the frame loop.
class Window {
public:
    [[nodiscard]] static auto create(const WindowDesc& desc) -> Result<Window>;

    Window(Window&& other) noexcept;
    auto operator=(Window&& other) noexcept -> Window&;
    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    ~Window();

    [[nodiscard]] auto should_close() const noexcept -> bool;
    // Programmatic close request — mirrors the Qt `QWindow::close()`
    // path so the App can drive shutdown from menu actions / Ctrl+Q.
    void set_should_close(bool value) noexcept;

    // Update the window's title bar text. The host calls this when
    // the document filename or dirty state changes.
    void set_title(std::string_view title) noexcept;

    // Window-control verbs — used by the custom Mac-style chrome's
    // traffic-light buttons (Phase 2 of ADR 0034). On a frameless
    // window these are the only way the user can minimise / maximise
    // / close; on a system-chrome window they back the same menu
    // actions the OS provides.
    void minimize() noexcept;
    void toggle_maximize() noexcept;
    void request_close() noexcept;
    [[nodiscard]] auto is_maximized() const noexcept -> bool;

    // Begin a system drag of the window from its current cursor
    // position. The chrome's drag-region click handler calls this
    // so the user can pick up the window by its custom title bar.
    void start_system_drag() noexcept;

    void poll_events() noexcept;

    [[nodiscard]] auto framebuffer_size() const noexcept -> std::pair<std::uint32_t, std::uint32_t>;

    // Vulkan surface for the held `QWindow`. The Window owns the
    // `QVulkanInstance` that bridges to `vk_instance`; the lifetime
    // of the returned `VkSurfaceKHR` is tied to the Window and Qt
    // tears it down on destruction. Replaces the v0.x
    // `glfwCreateWindowSurface` path.
    [[nodiscard]] auto create_surface(VkInstance vk_instance) -> Result<VkSurfaceKHR>;

    // Raw handle for callers that need the underlying QWindow. Returns
    // a `void*` to keep the platform header free of Qt forward
    // declarations; the ImGui input bridge casts back to `QWindow*`.
    [[nodiscard]] auto native_handle() const noexcept -> void*;

private:
    struct Impl;
    Window() noexcept;
    void destroy() noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace noted::platform
