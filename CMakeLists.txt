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

# CMake ships MSVC defaults with /Ob2 (inline expansion level 2) baked into
# CMAKE_CXX_FLAGS_RELEASE / RELWITHDEBINFO / MINSIZEREL. We add the more
# aggressive /Ob3 per-target in CompilerWarnings.cmake; without removing the
# baked-in /Ob2 first MSVC emits D9025 ('/Ob2' overridden by '/Ob3') on
# every Release TU. Strip /Ob[0-9] from the cached flags here so the
# per-target /Ob3 is the only inline directive that reaches cl.exe.
if(MSVC)
    foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
        if(DEFINED CMAKE_CXX_FLAGS${_cfg})
            string(REGEX REPLACE "[ ]?/Ob[0-9]" ""
                CMAKE_CXX_FLAGS${_cfg} "${CMAKE_CXX_FLAGS${_cfg}}")
        endif()
        if(DEFINED CMAKE_C_FLAGS${_cfg})
            string(REGEX REPLACE "[ ]?/Ob[0-9]" ""
                CMAKE_C_FLAGS${_cfg} "${CMAKE_C_FLAGS${_cfg}}")
        endif()
    endforeach()
endif()

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
option(NOTED_ENABLE_TRACY        "Enable Tracy profiler instrumentation" OFF)

# ---- External dependencies ----------------------------------------------
find_package(Vulkan 1.3 REQUIRED)

include(FetchContent)

# Every FetchContent_Declare below uses SYSTEM so the dependency's headers
# are exposed to consumers as system includes. Combined with MSVC's
# /external:anglebrackets + /external:W0 (in CompilerWarnings.cmake) and
# GCC/clang's implicit `-isystem` mapping, this silences warnings inside
# third-party headers (e.g., GLFW emits C4819 on Korean/Japanese code
# pages) without lowering the warning level for our own code.

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(glfw)

# Vulkan Memory Allocator — header-only with a CMake INTERFACE target.
# We compile the implementation in exactly one TU (engine/src/gpu/allocator.cpp).
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# stb_image — single-header PNG/JPG/BMP/TGA decoder. Implementation TU
# lives in platform/src/image_io/image_io.cpp via STB_IMAGE_IMPLEMENTATION.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
    SYSTEM
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# nlohmann/json — single-header JSON parser used by the `.noted` file
# format (domain/io/document_json). Header-only INTERFACE library.
# Pinned to a tagged release for reproducible builds across the matrix.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

# miniz — single-header zip read/write used by the `.noted` archive
# container (platform/io/noted_file). Public domain. Pinned to 3.0.2.
# Disable miniz's own tooling targets; we only need the library.
# CMAKE_POLICY_VERSION_MINIMUM bridges miniz's old
# `cmake_minimum_required(VERSION 2.8.12)` against modern CMake which
# refuses anything < 3.5. Confined to miniz's scope.
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(BUILD_FUZZERS  OFF CACHE INTERNAL "")
set(INSTALL_PROJECT OFF CACHE INTERNAL "")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(miniz)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

# nativefiledialog-extended — modern fork of mlabbe/nativefiledialog
# with IFileDialog on Win32, GTK3/portal on Linux, NSSavePanel on macOS.
# Zlib license. Used by platform::io::file_picker to back File →
# New / Open / Save in the v0.x product shell. CMake target name:
# `nfd`. SYSTEM so its headers don't generate warnings.
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.2.1
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(nfd)

# Dear ImGui — v0.x UI stack per ADR 0027. Upstream ships no
# CMakeLists.txt; FetchContent_MakeAvailable just populates the
# source tree, then we declare our own static lib target compiling
# the core TUs plus the Vulkan + GLFW backends.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5-docking
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
)
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
# ui/include is on imgui's path so it (and every consumer) finds
# noted/ui/imgui_user_config.hpp — the IM_ASSERT routing per ADR 0027.
# Marked SYSTEM so the ui/ headers visible through it don't generate
# warnings inside ImGui's own TUs.
target_include_directories(imgui SYSTEM PUBLIC
    "${CMAKE_SOURCE_DIR}/ui/include"
)
target_compile_definitions(imgui PUBLIC
    "IMGUI_USER_CONFIG=\"noted/ui/imgui_user_config.hpp\""
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)
# imgui's TUs reference noted::harness::validate via the user config's
# forward decl; the symbol resolves at final link time against
# noted::engine. Adding it to PUBLIC so any consumer linking imgui
# also pulls in the engine that provides validate().
target_link_libraries(imgui PUBLIC noted::engine)
add_library(imgui::imgui ALIAS imgui)

if(NOTED_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
        SYSTEM
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# Tracy — frame-grained profiler. Off by default. When enabled we set
# TRACY_ENABLE so the client compiles its full machinery, and TRACY_ON_DEMAND
# so the client only collects when a profiler is connected (safe for shipped
# builds that ship with Tracy compiled in). See docs/architecture/0013.
if(NOTED_ENABLE_TRACY)
    set(TRACY_ENABLE    ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
        GIT_SHALLOW TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(tracy)
endif()

# ---- Modules (order: leaves first) --------------------------------------
add_subdirectory(engine)
add_subdirectory(domain)
add_subdirectory(compositor)
add_subdirectory(plugin)
add_subdirectory(platform)
add_subdirectory(ui)
add_subdirectory(shaders)
add_subdirectory(app)

if(NOTED_BUILD_TESTS)
    add_subdirectory(tests)
endif()
