noted_add_module(
    NAME    engine
    SOURCES
        src/engine.cpp
        src/hook/registry.cpp
        src/error/error.cpp
        src/harness/harness.cpp
        src/gpu/gpu.cpp
        src/gpu/instance.cpp
        src/gpu/physical_device.cpp
        src/gpu/surface.cpp
        src/gpu/device.cpp
        src/gpu/swapchain.cpp
        src/gpu/command_pool.cpp
        src/gpu/command_buffer.cpp
        src/tile/tile.cpp
        src/color/color.cpp
        src/job/job.cpp
        src/memory/memory.cpp
    PUBLIC_DEPS
        Vulkan::Vulkan
        glfw
)

# Module-local tests live under //tests/unit (shared with other modules).
# When the engine grows GPU integration tests, this is where they slot in.
