noted_add_module(
    NAME    ui
    SOURCES
        src/imgui_host.cpp
        src/view/view.cpp
        src/widget/widget.cpp
        src/widget/menu_bar.cpp
        src/widget/layer_panel.cpp
        src/widget/outline_panel.cpp
        src/widget/page_strip.cpp
        src/widget/tool_palette.cpp
        src/widget/brush_options.cpp
        src/widget/selection_overlay.cpp
        src/widget/status_bar.cpp
        src/widget/debug_overlay.cpp
        src/theme/theme.cpp
        src/binding/binding.cpp
    PUBLIC_DEPS  noted::domain noted::platform noted::engine imgui
)
