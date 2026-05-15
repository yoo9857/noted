add_executable(noted_app src/main.cpp)

target_link_libraries(noted_app PRIVATE
    noted::engine
    noted::domain
    noted::platform
    noted::plugin
    noted::ui
)

target_compile_features(noted_app PRIVATE cxx_std_23)
noted_apply_warnings(noted_app)
noted_apply_hardening(noted_app)

set_target_properties(noted_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
