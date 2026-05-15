noted_add_module(
    NAME    platform
    SOURCES
        src/window/window.cpp
        src/input/input.cpp
        src/fs/fs.cpp
        src/ipc/ipc.cpp
        src/image_io/image_io.cpp
    PUBLIC_DEPS   noted::engine glfw
    PRIVATE_DEPS  stb_image
)
