noted_add_module(
    NAME    platform
    SOURCES
        src/window/window.cpp
        src/window/pen_input.cpp
        # Pen / stylus path picks per-OS impl. Windows uses WM_POINTER via
        # a subclass; everywhere else is a no-op stub today.
        $<$<PLATFORM_ID:Windows>:src/window/pen_input_win32.cpp>
        $<$<NOT:$<PLATFORM_ID:Windows>>:src/window/pen_input_stub.cpp>
        src/input/input.cpp
        src/fs/fs.cpp
        src/ipc/ipc.cpp
        src/image_io/image_io.cpp
    PUBLIC_DEPS   noted::engine glfw
    PRIVATE_DEPS  stb_image
)

# Comctl32 ships SetWindowSubclass / DefSubclassProc — required by the
# Windows pen-input subclass. Available on every Windows install.
if(WIN32)
    target_link_libraries(noted_platform PRIVATE Comctl32)
endif()
