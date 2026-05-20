noted_add_module(
    NAME    domain
    SOURCES
        src/document/document.cpp
        src/command/command.cpp
        src/io/document_json.cpp
        src/layer/layer.cpp
        src/selection/selection.cpp
        src/tool/tool.cpp
        src/tool/options.cpp
        src/tool/selection_drag.cpp
    PUBLIC_DEPS   noted::engine
    PRIVATE_DEPS  nlohmann_json::nlohmann_json
)

