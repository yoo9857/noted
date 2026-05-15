noted_add_module(
    NAME    engine
    SOURCES
        src/hook/registry.cpp
        src/error/error.cpp
        src/harness/harness.cpp
        src/gpu/gpu.cpp
        src/tile/tile.cpp
        src/color/color.cpp
        src/job/job.cpp
        src/memory/memory.cpp
    PUBLIC_DEPS
        Vulkan::Vulkan
        glfw
)

if(NOTED_BUILD_TESTS)
    add_subdirectory(tests)
endif()
