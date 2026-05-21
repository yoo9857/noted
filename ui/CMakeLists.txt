noted_add_module(
    NAME    ui
    SOURCES
        src/imgui_host.cpp
        src/imgui_input_bridge.cpp
        src/view/view.cpp
        src/widget/widget.cpp
        src/widget/color_picker_panel.cpp
        src/widget/mac_chrome.cpp
        src/widget/menu_bar.cpp
        src/widget/navigator_panel.cpp
        src/widget/layer_panel.cpp
        src/widget/outline_panel.cpp
        src/widget/page_strip.cpp
        src/widget/top_toolbar.cpp
        src/widget/brush_options.cpp
        src/widget/selection_overlay.cpp
        src/widget/shape_overlay.cpp
        src/widget/text_overlay.cpp
        src/widget/image_overlay.cpp
        src/widget/status_bar.cpp
        src/widget/debug_overlay.cpp
        src/theme/theme.cpp
        src/binding/binding.cpp
    PUBLIC_DEPS  noted::domain noted::platform noted::engine imgui Qt6::Core Qt6::Gui
)
