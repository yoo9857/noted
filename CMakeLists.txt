cmake_minimum_required(VERSION 3.28)

project(noted
    VERSION 0.0.1
    DESCRIPTION "Professional note + raster image editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Hardening)
include(NotedModule)

# ---- Project options ----------------------------------------------------
option(NOTED_BUILD_TESTS         "Build unit/integration/bench tests"  ON)
option(NOTED_WARNINGS_AS_ERRORS  "Treat warnings as errors"           OFF)
option(NOTED_ENABLE_ASAN         "Enable AddressSanitizer"            OFF)
option(NOTED_ENABLE_UBSAN        "Enable UndefinedBehaviorSanitizer"  OFF)
option(NOTED_ENABLE_TSAN         "Enable ThreadSanitizer"             OFF)

# ---- External dependencies ----------------------------------------------
find_package(Vulkan 1.3 REQUIRED)

include(FetchContent)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# Vulkan Memory Allocator — header-only with a CMake INTERFACE target.
# We compile the implementation in exactly one TU (engine/src/gpu/allocator.cpp).
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

if(NOTED_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ---- Modules (order: leaves first) --------------------------------------
add_subdirectory(engine)
add_subdirectory(domain)
add_subdirectory(plugin)
add_subdirectory(platform)
add_subdirectory(ui)
add_subdirectory(shaders)
add_subdirectory(app)

if(NOTED_BUILD_TESTS)
    add_subdirectory(tests)
endif()
