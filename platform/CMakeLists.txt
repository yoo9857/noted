noted_add_module(
    NAME    platform
    SOURCES
        src/window/window.cpp
        src/input/input.cpp
        src/fs/fs.cpp
        src/ipc/ipc.cpp
    PUBLIC_DEPS  noted::engine glfw
)
