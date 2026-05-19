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

# Polyline shader — two entries (vs_polyline + ps_polyline). The
# stroke engine tessellates each Stroke into a triangle-strip ribbon
# (Phase A.2.c). Supersedes the old stamp.slang per ADR 0015 →
# ADR 0029 vector-ink migration.
set(_polyline_src "${CMAKE_CURRENT_SOURCE_DIR}/polyline.slang")
noted_compile_slang_entry(
    SOURCE  "${_polyline_src}"
    ENTRY   vs_polyline
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/polyline.vs_polyline.spv"
)
noted_compile_slang_entry(
    SOURCE  "${_polyline_src}"
    ENTRY   ps_polyline
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/polyline.ps_polyline.spv"
)

# Layer compositor shader — two entries (vs_layer + ps_layer). One
# fullscreen triangle per layer; push-constant pre-multiplied RGBA.
set(_layer_src "${CMAKE_CURRENT_SOURCE_DIR}/layer.slang")
noted_compile_slang_entry(
    SOURCE  "${_layer_src}"
    ENTRY   vs_layer
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/layer.vs_layer.spv"
)
noted_compile_slang_entry(
    SOURCE  "${_layer_src}"
    ENTRY   ps_layer
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/layer.ps_layer.spv"
)

add_custom_target(noted_shaders ALL
    DEPENDS
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.vs_main.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_gradient.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen.ps_textured.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/polyline.vs_polyline.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/polyline.ps_polyline.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/layer.vs_layer.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/layer.ps_layer.spv"
)

set(NOTED_SHADER_OUTPUT_DIR "${NOTED_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
