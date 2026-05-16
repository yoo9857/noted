noted_add_module(
    NAME    domain
    SOURCES
        src/document/document.cpp
        src/command/command.cpp
        src/layer/layer.cpp
        src/selection/selection.cpp
        src/crdt/crdt.cpp
    PUBLIC_DEPS  noted::engine
)

