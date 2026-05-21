#include "noted/platform/window/window.hpp"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVulkanInstance>
#include <QWheelEvent>
#include <QWindow>
#include <atomic>
#include <string>
#include <utility>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/platform/window/pen_input.hpp"

#if defined(_WIN32)
#include <Windows.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
// DWMWA_WINDOW_CORNER_PREFERENCE (Win11 SDK member of the
// DWMWINDOWATTRIBUTE enum) may be absent in older SDKs even when
// the corner-preference enum itself is present. Provide a numeric
// fallback so the call still compiles; on Windows 10 + earlier
// DwmSetWindowAttribute returns an error code that we deliberately
// ignore — the custom chrome layer still renders identically.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#endif

namespace noted::platform {

namespace {

[[nodiscard]] auto map_button(Qt::MouseButton b) noexcept -> noted::hook::PointerButton {
    switch (b) {
        case Qt::LeftButton:
            return noted::hook::PointerButton::left;
        case Qt::RightButton:
            return noted::hook::PointerButton::right;
        case Qt::MiddleButton:
            return noted::hook::PointerButton::middle;
        default:
            return noted::hook::PointerButton::other;
    }
}

// Qt key → an integer the rest of the codebase can use through the
// hook channel. We retain `glfw_key` as the field name on
// KeyPressed / KeyReleased for now (post-migration alias rename is
// a separate slice) and emit raw `Qt::Key` values — every
// downstream caller compares against constants from
// `engine/hook/hook.hpp` which were already abstracted from raw
// GLFW values via the modifier-mask consumer in App, so the swap
// is invisible to the caller as long as the value stays a stable
// integer. The constants themselves are migrated to Qt::Key in a
// follow-up cleanup.
[[nodiscard]] auto qt_modifier_mask(Qt::KeyboardModifiers m) noexcept -> int {
    int out = 0;
    if (m & Qt::ShiftModifier) {
        out |= 0x0001;
    }
    if (m & Qt::ControlModifier) {
        out |= 0x0002;
    }
    if (m & Qt::AltModifier) {
        out |= 0x0004;
    }
    if (m & Qt::MetaModifier) {
        out |= 0x0008;
    }
    return out;
}

// QWindow subclass — intercepts platform events and re-publishes
// them on the global hook registry. Keeps the rest of the engine
// platform-agnostic; downstream subscribers (camera controller,
// stroke engine, tool router, ImGui input bridge) never see Qt
// types directly.
class NotedQWindow : public QWindow {
public:
    NotedQWindow() noexcept { setSurfaceType(QSurface::VulkanSurface); }

    void set_should_close(bool v) noexcept { should_close_.store(v, std::memory_order_release); }
    [[nodiscard]] auto should_close() const noexcept -> bool {
        return should_close_.load(std::memory_order_acquire);
    }

protected:
    auto event(QEvent* e) -> bool override { return QWindow::event(e); }

    void mousePressEvent(QMouseEvent* e) override {
        const auto px = e->position();
        noted::hook::registry().on_pointer_pressed.publish(noted::hook::PointerPressed{
            .x = px.x() * devicePixelRatio(),
            .y = px.y() * devicePixelRatio(),
            .button = map_button(e->button()),
        });
    }

    void mouseReleaseEvent(QMouseEvent* e) override {
        const auto px = e->position();
        noted::hook::registry().on_pointer_released.publish(noted::hook::PointerReleased{
            .x = px.x() * devicePixelRatio(),
            .y = px.y() * devicePixelRatio(),
            .button = map_button(e->button()),
        });
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const auto px = e->position();
        noted::hook::registry().on_pointer_moved.publish(noted::hook::PointerMoved{
            .x = px.x() * devicePixelRatio(),
            .y = px.y() * devicePixelRatio(),
        });
    }

    void wheelEvent(QWheelEvent* e) override {
        // angleDelta is in eighths of a degree, 120/notch on a mouse.
        // Normalize to "notches" so a single mouse-wheel click yields
        // dy = 1.0 — same numeric scale GLFW produced.
        const auto delta = e->angleDelta();
        noted::hook::registry().on_scrolled.publish(noted::hook::Scrolled{
            .dx = static_cast<double>(delta.x()) / 120.0,
            .dy = static_cast<double>(delta.y()) / 120.0,
        });
    }

    void keyPressEvent(QKeyEvent* e) override {
        noted::hook::registry().on_key_pressed.publish(noted::hook::KeyPressed{
            .glfw_key = static_cast<int>(e->key()),
            .scancode = static_cast<int>(e->nativeScanCode()),
            .mods = qt_modifier_mask(e->modifiers()),
            .is_repeat = e->isAutoRepeat(),
        });
    }

    void keyReleaseEvent(QKeyEvent* e) override {
        noted::hook::registry().on_key_released.publish(noted::hook::KeyReleased{
            .glfw_key = static_cast<int>(e->key()),
            .scancode = static_cast<int>(e->nativeScanCode()),
            .mods = qt_modifier_mask(e->modifiers()),
        });
    }

    void resizeEvent(QResizeEvent* e) override {
        const auto fb_w = e->size().width() * devicePixelRatio();
        const auto fb_h = e->size().height() * devicePixelRatio();
        noted::hook::registry().on_framebuffer_resized.publish(noted::hook::FramebufferResized{
            .width = static_cast<std::uint32_t>(fb_w),
            .height = static_cast<std::uint32_t>(fb_h),
        });
    }

    void closeEvent(QCloseEvent* e) override {
        should_close_.store(true, std::memory_order_release);
        // Accept the event but the renderer will see should_close() ==
        // true on its next loop iteration and tear down cleanly.
        e->accept();
    }

private:
    std::atomic<bool> should_close_{false};
};

}  // namespace

struct Window::Impl {
    std::unique_ptr<NotedQWindow> window;
    std::unique_ptr<QVulkanInstance> qt_vk_instance;
};

Window::Window() noexcept : impl_(std::make_unique<Impl>()) {}

auto Window::create(const WindowDesc& desc) -> Result<Window> {
    // QGuiApplication must already exist on the calling thread —
    // App's main() instantiates it before any Window::create call.
    // We refuse to silently construct one here because the
    // lifetime would not survive the Window's destructor (Qt's
    // QGuiApplication must outlive every QWindow it owns).
    if (QCoreApplication::instance() == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state,
            "Window::create: QGuiApplication must be constructed by the host before "
            "any Window — see app/src/main.cpp"));
    }

    Window self;
    self.impl_->window = std::make_unique<NotedQWindow>();
    auto* w = self.impl_->window.get();
    w->setTitle(QString::fromUtf8(desc.title.data(), static_cast<int>(desc.title.size())));
    w->resize(static_cast<int>(desc.width), static_cast<int>(desc.height));
    if (desc.frameless) {
        // Hide the OS-native title bar / chrome. The app paints its
        // own Mac-style chrome (see `ui::widget::mac_chrome`).
        w->setFlags(w->flags() | Qt::FramelessWindowHint);
    }
    // High-DPI awareness is the Qt 6 default; nothing to opt into.

    // Attach the pen-input subsystem. On Windows this installs a
    // WM_POINTER subclass that publishes events with real pressure
    // / tilt. Elsewhere it's a documented no-op (see ADR 0017).
    // The Qt-side QTabletEvent path is a follow-up; for now we
    // keep the Win32 subclass that was already shipping pre-
    // migration.
    w->create();  // realises the native HWND so getWinId returns it
#if defined(_WIN32)
    void* native = reinterpret_cast<void*>(w->winId());
#else
    void* native = nullptr;
#endif
    if (auto r = noted::platform::pen::install_pen_input(native); !r) {
        // Pen input is an enhancement — failing to install should
        // NOT abort window creation. Log via the hook channel and
        // continue.
        noted::hook::registry().on_error.publish(noted::hook::ErrorObserved{
            .error = std::move(r).error(),
            .recoverable = true,
        });
    }

    w->show();

#if defined(_WIN32)
    // Win11 DWM rounded corners on the native window frame. The
    // attribute is a no-op on Windows 10 (DwmSetWindowAttribute
    // returns failure silently) — the chrome layer above still
    // delivers its custom title bar regardless.
    if (desc.frameless) {
        const HWND hwnd = reinterpret_cast<HWND>(w->winId());
        DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
        (void) DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    }
#endif
    return self;
}

Window::Window(Window&& other) noexcept : impl_(std::move(other.impl_)) {}

auto Window::operator=(Window&& other) noexcept -> Window& {
    if (this != &other) {
        destroy();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Window::~Window() {
    destroy();
}

void Window::destroy() noexcept {
    if (impl_ && impl_->window) {
#if defined(_WIN32)
        noted::platform::pen::uninstall_pen_input(reinterpret_cast<void*>(impl_->window->winId()));
#endif
        impl_->window.reset();
        impl_->qt_vk_instance.reset();
    }
}

auto Window::should_close() const noexcept -> bool {
    return impl_ && impl_->window && impl_->window->should_close();
}

void Window::set_should_close(bool value) noexcept {
    if (impl_ && impl_->window) {
        impl_->window->set_should_close(value);
    }
}

void Window::set_title(std::string_view title) noexcept {
    if (impl_ && impl_->window) {
        impl_->window->setTitle(QString::fromUtf8(title.data(), static_cast<int>(title.size())));
    }
}

void Window::minimize() noexcept {
    if (impl_ && impl_->window) {
        impl_->window->showMinimized();
    }
}

void Window::toggle_maximize() noexcept {
    if (!impl_ || !impl_->window) {
        return;
    }
    if (impl_->window->visibility() == QWindow::Maximized) {
        impl_->window->showNormal();
    } else {
        impl_->window->showMaximized();
    }
}

void Window::request_close() noexcept {
    if (impl_ && impl_->window) {
        impl_->window->set_should_close(true);
    }
}

auto Window::is_maximized() const noexcept -> bool {
    return impl_ && impl_->window && impl_->window->visibility() == QWindow::Maximized;
}

void Window::start_system_drag() noexcept {
    if (impl_ && impl_->window) {
        impl_->window->startSystemMove();
    }
}

void Window::poll_events() noexcept {
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::processEvents();
    }
}

auto Window::framebuffer_size() const noexcept -> std::pair<std::uint32_t, std::uint32_t> {
    if (!impl_ || !impl_->window) {
        return {0, 0};
    }
    const auto sz = impl_->window->size();
    const auto dpr = impl_->window->devicePixelRatio();
    return {static_cast<std::uint32_t>(sz.width() * dpr),
            static_cast<std::uint32_t>(sz.height() * dpr)};
}

auto Window::create_surface(VkInstance vk_instance) -> Result<VkSurfaceKHR> {
    if (!impl_ || !impl_->window) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "Window::create_surface: window not initialised"));
    }
    // Wrap our existing VkInstance in a QVulkanInstance so QWindow
    // can mint a VkSurfaceKHR through Qt's per-platform surface
    // factory. We do not let QVulkanInstance create the VkInstance
    // itself — App owns the instance + the validation layer
    // configuration + the extension list. Qt is purely the
    // surface-bridge here.
    impl_->qt_vk_instance = std::make_unique<QVulkanInstance>();
    impl_->qt_vk_instance->setVkInstance(vk_instance);
    if (!impl_->qt_vk_instance->create()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_surface_lost,
                              std::string{"QVulkanInstance::create failed: errorCode="} +
                                  std::to_string(impl_->qt_vk_instance->errorCode())));
    }
    impl_->window->setVulkanInstance(impl_->qt_vk_instance.get());
    const VkSurfaceKHR surface = QVulkanInstance::surfaceForWindow(impl_->window.get());
    if (surface == VK_NULL_HANDLE) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_surface_lost,
                              "QVulkanInstance::surfaceForWindow returned VK_NULL_HANDLE"));
    }
    return surface;
}

auto Window::native_handle() const noexcept -> void* {
    return impl_ && impl_->window ? static_cast<void*>(impl_->window.get()) : nullptr;
}

}  // namespace noted::platform
