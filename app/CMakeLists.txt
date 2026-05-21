add_executable(noted_app
    src/main.cpp
    src/app.cpp
    src/config/app_config.cpp
    src/scene/demo_scene.cpp
    src/io/font_probe.cpp
    src/ui/document_session.cpp
    src/ui/dirty_prompt.cpp
    src/input/tool_input_router.cpp
    src/input/selection_tool_handler.cpp
    src/input/shape_tool_handler.cpp
    src/input/text_tool_handler.cpp
    src/input/image_tool_handler.cpp
    src/input/camera_controller.cpp
    src/input/shape_recognizer.cpp
    src/frame/render_passes.cpp
    src/ui/ui_panels.cpp
)

# App-internal headers (src/app.hpp, src/scene/*.hpp, ...) are
# referenced as `"scene/demo_scene.hpp"` etc. — relative to `src/`.
target_include_directories(noted_app PRIVATE src)

target_link_libraries(noted_app PRIVATE
    noted::engine
    noted::domain
    noted::compositor
    noted::platform
    noted::plugin
    noted::ui
    imgui
    nlohmann_json::nlohmann_json
)

# Ensure the .spv files are built before noted_app links so they're on disk
# when the binary first runs. NOTED_SHADER_OUTPUT_DIR comes from
# shaders/CMakeLists.txt.
add_dependencies(noted_app noted_shaders)

target_compile_definitions(noted_app PRIVATE
    NOTED_SHADER_DIR="${NOTED_SHADER_OUTPUT_DIR}"
)

target_compile_features(noted_app PRIVATE cxx_std_23)
noted_apply_warnings(noted_app)
noted_apply_hardening(noted_app)

set_target_properties(noted_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Production layout: copy compiled .spv shaders next to the .exe so
# `resolve_shader_dir(cfg)` finds them via the `<exe_dir>/shaders`
# candidate. This makes the build output zip-distributable on
# Windows without a separate package step. The dev workflow (running
# straight from `build/bin/`) gets the production layout for free,
# and `NOTED_SHADER_DIR` (compile-time + env) stays available as
# fallback for cases where the copy hasn't run yet.
add_custom_command(TARGET noted_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${NOTED_SHADER_OUTPUT_DIR}"
        "$<TARGET_FILE_DIR:noted_app>/shaders"
    COMMENT "Copy compiled shaders to <exe_dir>/shaders"
    VERBATIM
)
