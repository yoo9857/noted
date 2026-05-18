noted_add_module(
    NAME    ui
    SOURCES
        src/imgui_host.cpp
        src/view/view.cpp
        src/widget/widget.cpp
        src/theme/theme.cpp
        src/binding/binding.cpp
    PUBLIC_DEPS  noted::domain noted::platform imgui
)
