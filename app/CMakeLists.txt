add_executable(noted_app src/main.cpp)

target_link_libraries(noted_app PRIVATE
    noted::engine
    noted::domain
    noted::compositor
    noted::platform
    noted::plugin
    noted::ui
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
