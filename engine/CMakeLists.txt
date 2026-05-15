add_executable(noted_app src/main.cpp)

target_link_libraries(noted_app PRIVATE
    Vulkan::Vulkan
    glfw
)

target_compile_features(noted_app PRIVATE cxx_std_23)

set_target_properties(noted_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
