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
    stroke_geometry_test.cpp
    page_test.cpp
    page_strip_test.cpp
    document_pages_test.cpp
    document_shapes_test.cpp
    document_texts_test.cpp
    document_images_test.cpp
    document_strokes_test.cpp
    canvas_layer_stack_test.cpp
    document_canvas_layers_test.cpp
    document_canvas_layers_json_test.cpp
    brush_library_test.cpp
    image_asset_registry_test.cpp
    shape_detect_test.cpp
    shape_recognizer_test.cpp
    clipboard_test.cpp
    lasso_polygon_test.cpp
    tool_test.cpp
    tool_options_test.cpp
    selection_drag_test.cpp
    shape_drag_test.cpp
    text_input_test.cpp
    image_input_test.cpp
    image_io_test.cpp
    tool_input_router_test.cpp
    camera_controller_test.cpp
    # App-private translation units exercised from tests. These compile
    # alongside `noted_app` (separate executable, no link conflict).
    # Tests reach them via the `app/src` include directory added below.
    ${CMAKE_SOURCE_DIR}/app/src/input/tool_input_router.cpp
    ${CMAKE_SOURCE_DIR}/app/src/input/selection_tool_handler.cpp
    ${CMAKE_SOURCE_DIR}/app/src/input/camera_controller.cpp
    ${CMAKE_SOURCE_DIR}/app/src/input/shape_recognizer.cpp
    # `app_config.cpp` pulls nlohmann/json (PRIVATE_DEP on domain).
    # Tests only need the `CanvasConfig` struct from the header; the
    # parser isn't exercised, so we don't compile the .cpp here.
)

target_include_directories(noted_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/app/src)
target_link_libraries(noted_unit_tests PRIVATE noted_test_harness noted::domain noted::compositor noted::platform noted::ui imgui)
target_compile_features(noted_unit_tests PRIVATE cxx_std_23)
noted_apply_warnings(noted_unit_tests)

gtest_discover_tests(noted_unit_tests)
