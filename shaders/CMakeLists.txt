include(Shaders)

set(NOTED_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

# Single Slang file, three entries: vs_main, ps_gradient, ps_textured.
# Each entry is compiled to its own SPIR-V module so the app can pick
# the fragment shader it needs at pipeline-build time.
set(_slang_src "${CMAKE_CURRENT_SOURCE_DIR}/fullscreen.slang")

noted_compile_slang_entry(
    SOURCE  "${_slang_src}"
    ENTRY   vs_main
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.vs_main.spv"
)
noted_compile_slang_entry(
    SOURCE  "${_slang_src}"
    ENTRY   ps_gradient
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_gradient.spv"
)
noted_compile_slang_entry(
    SOURCE  "${_slang_src}"
    ENTRY   ps_textured
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_textured.spv"
)

# Stamp shader — two entries (vs_stamp + ps_stamp). Used by the stroke
# engine to draw disk stamps into the canvas. See stamp.slang for the
# push-constant contract.
set(_stamp_src "${CMAKE_CURRENT_SOURCE_DIR}/stamp.slang")
noted_compile_slang_entry(
    SOURCE  "${_stamp_src}"
    ENTRY   vs_stamp
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/stamp.vs_stamp.spv"
)
noted_compile_slang_entry(
    SOURCE  "${_stamp_src}"
    ENTRY   ps_stamp
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/stamp.ps_stamp.spv"
)

add_custom_target(noted_shaders ALL
    DEPENDS
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.vs_main.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_gradient.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_textured.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/stamp.vs_stamp.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/stamp.ps_stamp.spv"
)

set(NOTED_SHADER_OUTPUT_DIR "${NOTED_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
