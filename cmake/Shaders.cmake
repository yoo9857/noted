# Shader compilation: Slang -> SPIR-V via slangc.
#
# Slang ships with the Vulkan SDK 1.4+. A single .slang file declares
# multiple entry points via [shader("...")] attributes; we invoke
# slangc once per entry to produce one .spv per pipeline stage.
#
# Usage:
#   noted_compile_slang_entry(
#       SOURCE  shaders/fullscreen.slang
#       ENTRY   vs_main
#       OUTPUT  ${CMAKE_BINARY_DIR}/shaders/fullscreen.vs_main.spv
#   )
#
# The OUTPUT depends on the SOURCE, so re-editing the .slang file
# triggers a rebuild for every entry it exposes.

find_program(NOTED_SLANGC
    NAMES slangc
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES Bin bin
    DOC "Slang shader compiler (ships with the Vulkan SDK >= 1.4)"
)
if(NOT NOTED_SLANGC)
    message(FATAL_ERROR
        "slangc not found. Install the Vulkan SDK >= 1.4 and ensure VULKAN_SDK "
        "is set (see docs/SETUP.md).")
endif()

function(noted_compile_slang_entry)
    set(options)
    set(oneValueArgs SOURCE ENTRY OUTPUT PROFILE)
    set(multiValueArgs)
    cmake_parse_arguments(SH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SH_SOURCE OR NOT SH_ENTRY OR NOT SH_OUTPUT)
        message(FATAL_ERROR
            "noted_compile_slang_entry: SOURCE, ENTRY, and OUTPUT are required")
    endif()
    if(NOT SH_PROFILE)
        set(SH_PROFILE "spirv_1_5")
    endif()

    get_filename_component(_out_dir "${SH_OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${_out_dir}")

    add_custom_command(
        OUTPUT "${SH_OUTPUT}"
        COMMAND "${NOTED_SLANGC}"
                "${SH_SOURCE}"
                -target spirv
                -profile ${SH_PROFILE}
                -entry ${SH_ENTRY}
                -O3
                -emit-spirv-directly
                -o "${SH_OUTPUT}"
        DEPENDS "${SH_SOURCE}"
        COMMENT "Slang -> SPIR-V: ${SH_SOURCE} @ ${SH_ENTRY}"
        VERBATIM
    )
endfunction()
