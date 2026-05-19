add_executable(noted_unit_tests
    error_test.cpp
    hook_test.cpp
    harness_test.cpp
    engine_test.cpp
    stroke_engine_test.cpp
    layer_graph_test.cpp
    layer_compositor_test.cpp
    pen_input_test.cpp
    selection_test.cpp
    selection_rasterizer_test.cpp
    document_test.cpp
    command_test.cpp
    document_json_test.cpp
    noted_file_test.cpp
    imgui_host_test.cpp
    camera_test.cpp
)

target_link_libraries(noted_unit_tests PRIVATE noted_test_harness noted::domain noted::compositor noted::platform noted::ui)
target_compile_features(noted_unit_tests PRIVATE cxx_std_23)
noted_apply_warnings(noted_unit_tests)

gtest_discover_tests(noted_unit_tests)
