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
    src/input/lasso_tool_handler.cpp
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
    # Qt 6 — Phase 0 of ADR 0034. Linked but not yet used; phase 1
    # (QGuiApplication + QWindow shell swap) is the first PR with
    # Qt-using code. Linking up front lets every subsequent phase
    # build incrementally without touching CMake.
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
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

# Windows: deploy Qt runtime DLLs + platform plugins next to the
# exe. Without this, launching `build/bin/noted_app.exe` on a
# machine that doesn't have Qt's bin/ on PATH (the typical dev
# setup with `aqt install-qt`) pops a "missing qwindows.dll"
# error dialog before any of our code runs. windeployqt ships in
# Qt's bin/ directory and resolves the dependency graph for the
# bound binary, so the post-build step keeps the output
# zip-distributable on Windows the same way the shader copy
# above does. macOS / Linux have their own deployment tools
# (`macdeployqt` / `linuxdeployqt`); skip those here — the dev
# install on those platforms typically resolves Qt via rpath.
if(WIN32)
    find_program(NOTED_WINDEPLOYQT NAMES windeployqt
        HINTS
            "$<TARGET_FILE_DIR:Qt6::qmake>"
            "${Qt6_DIR}/../../../bin"
        DOC "Path to Qt's windeployqt.exe; auto-discovered from the Qt6 package"
    )
    if(NOTED_WINDEPLOYQT)
        add_custom_command(TARGET noted_app POST_BUILD
            COMMAND "${NOTED_WINDEPLOYQT}"
                --release
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:noted_app>"
            COMMENT "windeployqt: copy Qt runtime + platform plugins to <exe_dir>"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt not found alongside the Qt6 install — "
            "noted_app.exe will fail to launch with 'qwindows.dll missing' "
            "unless Qt's bin/ is on PATH at runtime. Set CMAKE_PREFIX_PATH "
            "to a Qt install whose bin/ contains windeployqt.exe.")
    endif()
endif()
