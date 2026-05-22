noted_add_module(
    NAME    domain
    SOURCES
        src/document/document.cpp
        src/document/image_asset_registry.cpp
        src/command/command.cpp
        src/io/document_json.cpp
        src/layer/layer.cpp
        src/layer/canvas_layer_stack.cpp
        src/clipboard/selection_ops.cpp
        src/selection/selection.cpp
        src/shape_detect/shape_detect.cpp
        src/tool/tool.cpp
        src/tool/options.cpp
        src/tool/selection_drag.cpp
        src/tool/shape_drag.cpp
        src/tool/text_input.cpp
        src/tool/image_input.cpp
    PUBLIC_DEPS   noted::engine
    PRIVATE_DEPS  nlohmann_json::nlohmann_json
)

