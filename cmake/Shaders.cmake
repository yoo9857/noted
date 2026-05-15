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

    # slangc on the LunarG Linux SDK ships its plugins (slang-glslang,
    # spirv-opt) next to the binary but without an rpath set. Without
    # LD_LIBRARY_PATH pointing at $VULKAN_SDK/lib slangc reports
    # "failed to load dynamic library 'slang-glslang'" / 'pthread'.
    # Wrap with `cmake -E env` so the lib path is in scope per invocation.
    # -O3 + -emit-spirv-directly need spirv-opt at runtime; both are
    # dropped here for portability. Optimization comes back once the env
    # is verified across all matrix entries.
    if(WIN32)
        set(_slangc_cmd "${NOTED_SLANGC}")
    else()
        set(_slangc_cmd
            ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=$ENV{VULKAN_SDK}/lib:$ENV{LD_LIBRARY_PATH}"
            "${NOTED_SLANGC}")
    endif()

    add_custom_command(
        OUTPUT "${SH_OUTPUT}"
        COMMAND ${_slangc_cmd}
                "${SH_SOURCE}"
                -target spirv
                -profile ${SH_PROFILE}
                -entry ${SH_ENTRY}
                -o "${SH_OUTPUT}"
        DEPENDS "${SH_SOURCE}"
        COMMENT "Slang -> SPIR-V: ${SH_SOURCE} @ ${SH_ENTRY}"
        VERBATIM
    )
endfunction()
