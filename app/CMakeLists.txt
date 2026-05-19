add_executable(noted_app
    src/main.cpp
    src/app.cpp
    src/scene/demo_scene.cpp
    src/io/font_probe.cpp
    src/ui/document_session.cpp
    src/ui/dirty_prompt.cpp
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
