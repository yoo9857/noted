#include "noted/ui/imgui_input_bridge.hpp"

#include <Qt>

#include <imgui.h>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::ui {

namespace {

// Translate a Qt::Key (stored as int in `hook::KeyPressed::glfw_key`
// since the migration in `platform/window/window.cpp`) to the
// corresponding `ImGuiKey`. Returns ImGuiKey_None for keys ImGui
// doesn't care about — passing ImGuiKey_None to AddKeyEvent is
// inert.
//
// We cover the keys ImGui needs for its built-in navigation +
// keyboard-chord handling (modifiers + alphanumerics + arrow keys
// + enter / escape / backspace / delete / tab). The full Qt::Key
// enum has hundreds of values; the unmapped ones simply don't
// participate in ImGui's input.
[[nodiscard]] auto qt_key_to_imgui(int qt_key) noexcept -> ImGuiKey {
    switch (qt_key) {
        case Qt::Key_Tab:
            return ImGuiKey_Tab;
        case Qt::Key_Left:
            return ImGuiKey_LeftArrow;
        case Qt::Key_Right:
            return ImGuiKey_RightArrow;
        case Qt::Key_Up:
            return ImGuiKey_UpArrow;
        case Qt::Key_Down:
            return ImGuiKey_DownArrow;
        case Qt::Key_PageUp:
            return ImGuiKey_PageUp;
        case Qt::Key_PageDown:
            return ImGuiKey_PageDown;
        case Qt::Key_Home:
            return ImGuiKey_Home;
        case Qt::Key_End:
            return ImGuiKey_End;
        case Qt::Key_Insert:
            return ImGuiKey_Insert;
        case Qt::Key_Delete:
            return ImGuiKey_Delete;
        case Qt::Key_Backspace:
            return ImGuiKey_Backspace;
        case Qt::Key_Space:
            return ImGuiKey_Space;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return ImGuiKey_Enter;
        case Qt::Key_Escape:
            return ImGuiKey_Escape;
        case Qt::Key_Apostrophe:
            return ImGuiKey_Apostrophe;
        case Qt::Key_Comma:
            return ImGuiKey_Comma;
        case Qt::Key_Minus:
            return ImGuiKey_Minus;
        case Qt::Key_Period:
            return ImGuiKey_Period;
        case Qt::Key_Slash:
            return ImGuiKey_Slash;
        case Qt::Key_Semicolon:
            return ImGuiKey_Semicolon;
        case Qt::Key_Equal:
            return ImGuiKey_Equal;
        case Qt::Key_BracketLeft:
            return ImGuiKey_LeftBracket;
        case Qt::Key_Backslash:
            return ImGuiKey_Backslash;
        case Qt::Key_BracketRight:
            return ImGuiKey_RightBracket;
        case Qt::Key_QuoteLeft:
            return ImGuiKey_GraveAccent;
        case Qt::Key_CapsLock:
            return ImGuiKey_CapsLock;
        case Qt::Key_ScrollLock:
            return ImGuiKey_ScrollLock;
        case Qt::Key_NumLock:
            return ImGuiKey_NumLock;
        case Qt::Key_Print:
            return ImGuiKey_PrintScreen;
        case Qt::Key_Pause:
            return ImGuiKey_Pause;
        case Qt::Key_Shift:
            return ImGuiKey_LeftShift;
        case Qt::Key_Control:
            return ImGuiKey_LeftCtrl;
        case Qt::Key_Alt:
            return ImGuiKey_LeftAlt;
        case Qt::Key_Meta:
            return ImGuiKey_LeftSuper;
        default:
            break;
    }
    if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (qt_key - Qt::Key_0));
    }
    if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (qt_key - Qt::Key_A));
    }
    if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (qt_key - Qt::Key_F1));
    }
    return ImGuiKey_None;
}

// Bit mask we publish in `KeyPressed::mods` from
// `platform/window/window.cpp::qt_modifier_mask`. Decoding it here
// keeps the bridge in sync with the platform's chosen wire format
// (we control both ends).
constexpr int kModShift = 0x0001;
constexpr int kModControl = 0x0002;
constexpr int kModAlt = 0x0004;
constexpr int kModSuper = 0x0008;

[[nodiscard]] auto map_pointer_button(noted::hook::PointerButton b) noexcept -> int {
    switch (b) {
        case noted::hook::PointerButton::left:
            return 0;
        case noted::hook::PointerButton::right:
            return 1;
        case noted::hook::PointerButton::middle:
            return 2;
        default:
            return -1;
    }
}

}  // namespace

struct ImGuiInputBridge::Impl {
    noted::hook::Subscription<noted::hook::PointerMoved> sub_moved;
    noted::hook::Subscription<noted::hook::PointerPressed> sub_pressed;
    noted::hook::Subscription<noted::hook::PointerReleased> sub_released;
    noted::hook::Subscription<noted::hook::Scrolled> sub_scrolled;
    noted::hook::Subscription<noted::hook::KeyPressed> sub_key_pressed;
    noted::hook::Subscription<noted::hook::KeyReleased> sub_key_released;
};

ImGuiInputBridge::ImGuiInputBridge() noexcept : impl_(std::make_unique<Impl>()) {}

ImGuiInputBridge::~ImGuiInputBridge() = default;

auto ImGuiInputBridge::create() -> std::unique_ptr<ImGuiInputBridge> {
    // Heap-allocated for stable `this` — the hook lambdas capture
    // the bridge and Qt's event delivery thread is the same as the
    // engine thread that constructed it, so no synchronisation is
    // needed.
    auto bridge = std::unique_ptr<ImGuiInputBridge>(new ImGuiInputBridge);
    bridge->install_();
    return bridge;
}

void ImGuiInputBridge::install_() {
    auto& reg = noted::hook::registry();

    impl_->sub_moved = noted::hook::Subscription<noted::hook::PointerMoved>{
        reg.on_pointer_moved,
        reg.on_pointer_moved.subscribe([](const noted::hook::PointerMoved& e) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(static_cast<float>(e.x), static_cast<float>(e.y));
        })};

    impl_->sub_pressed = noted::hook::Subscription<noted::hook::PointerPressed>{
        reg.on_pointer_pressed,
        reg.on_pointer_pressed.subscribe([](const noted::hook::PointerPressed& e) {
            const auto btn = map_pointer_button(e.button);
            if (btn < 0) {
                return;
            }
            ImGui::GetIO().AddMouseButtonEvent(btn, true);
        })};

    impl_->sub_released = noted::hook::Subscription<noted::hook::PointerReleased>{
        reg.on_pointer_released,
        reg.on_pointer_released.subscribe([](const noted::hook::PointerReleased& e) {
            const auto btn = map_pointer_button(e.button);
            if (btn < 0) {
                return;
            }
            ImGui::GetIO().AddMouseButtonEvent(btn, false);
        })};

    impl_->sub_scrolled = noted::hook::Subscription<noted::hook::Scrolled>{
        reg.on_scrolled, reg.on_scrolled.subscribe([](const noted::hook::Scrolled& e) {
            ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(e.dx), static_cast<float>(e.dy));
        })};

    impl_->sub_key_pressed = noted::hook::Subscription<noted::hook::KeyPressed>{
        reg.on_key_pressed, reg.on_key_pressed.subscribe([](const noted::hook::KeyPressed& e) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & kModControl) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (e.mods & kModShift) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (e.mods & kModAlt) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (e.mods & kModSuper) != 0);
            const auto key = qt_key_to_imgui(e.glfw_key);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, true);
            }
        })};

    impl_->sub_key_released = noted::hook::Subscription<noted::hook::KeyReleased>{
        reg.on_key_released, reg.on_key_released.subscribe([](const noted::hook::KeyReleased& e) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, (e.mods & kModControl) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (e.mods & kModShift) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (e.mods & kModAlt) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (e.mods & kModSuper) != 0);
            const auto key = qt_key_to_imgui(e.glfw_key);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, false);
            }
        })};
}

void ImGuiInputBridge::new_frame(float framebuffer_w_px,
                                 float framebuffer_h_px,
                                 float delta_seconds) noexcept {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(framebuffer_w_px > 0.0F ? framebuffer_w_px : 1.0F,
                            framebuffer_h_px > 0.0F ? framebuffer_h_px : 1.0F);
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    io.DeltaTime = delta_seconds > 0.0F ? delta_seconds : (1.0F / 60.0F);
}

}  // namespace noted::ui
