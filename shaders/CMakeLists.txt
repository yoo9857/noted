include(Shaders)

set(NOTED_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

noted_compile_shader(
    SOURCE  "${CMAKE_CURRENT_SOURCE_DIR}/fullscreen_triangle.vert"
    STAGE   vertex
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/fullscreen_triangle.vert.spv"
)
noted_compile_shader(
    SOURCE  "${CMAKE_CURRENT_SOURCE_DIR}/fullscreen_triangle.frag"
    STAGE   fragment
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/fullscreen_triangle.frag.spv"
)
noted_compile_shader(
    SOURCE  "${CMAKE_CURRENT_SOURCE_DIR}/textured_quad.frag"
    STAGE   fragment
    OUTPUT  "${NOTED_SHADER_OUTPUT_DIR}/textured_quad.frag.spv"
)

add_custom_target(noted_shaders ALL
    DEPENDS
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen_triangle.vert.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/fullscreen_triangle.frag.spv"
        "${NOTED_SHADER_OUTPUT_DIR}/textured_quad.frag.spv"
)

# Export the directory so the app can find SPIR-V at runtime.
set(NOTED_SHADER_OUTPUT_DIR "${NOTED_SHADER_OUTPUT_DIR}" PARENT_SCOPE)
