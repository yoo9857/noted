# Shader compilation: GLSL -> SPIR-V via glslc.
#
# Usage:
#   noted_compile_shader(
#       SOURCE   shaders/fullscreen_triangle.vert
#       STAGE    vertex
#       OUTPUT   ${CMAKE_BINARY_DIR}/shaders/fullscreen_triangle.vert.spv
#   )
#
# Then add the OUTPUT to the executable's `add_dependencies` or to a
# custom target that the executable depends on, so the .spv is rebuilt
# whenever the source changes.

find_program(NOTED_GLSLC
    NAMES glslc
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES bin
    DOC "glslc shader compiler (ships with the Vulkan SDK)"
)
if(NOT NOTED_GLSLC)
    message(FATAL_ERROR
        "glslc not found. Install the Vulkan SDK and ensure VULKAN_SDK is set "
        "(see docs/SETUP.md).")
endif()

function(noted_compile_shader)
    set(options)
    set(oneValueArgs SOURCE STAGE OUTPUT)
    set(multiValueArgs)
    cmake_parse_arguments(SH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SH_SOURCE OR NOT SH_STAGE OR NOT SH_OUTPUT)
        message(FATAL_ERROR "noted_compile_shader: SOURCE, STAGE, and OUTPUT are required")
    endif()

    get_filename_component(_out_dir "${SH_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_out_dir}")

    add_custom_command(
        OUTPUT "${SH_OUTPUT}"
        COMMAND "${NOTED_GLSLC}"
                -O
                --target-env=vulkan1.3
                -fshader-stage=${SH_STAGE}
                -o "${SH_OUTPUT}"
                "${SH_SOURCE}"
        DEPENDS "${SH_SOURCE}"
        COMMENT "GLSL -> SPIR-V: ${SH_SOURCE}"
        VERBATIM
    )
endfunction()
