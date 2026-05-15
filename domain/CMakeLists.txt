noted_add_module(
    NAME    domain
    SOURCES
        src/document/document.cpp
        src/command/command.cpp
        src/layer/layer.cpp
        src/crdt/crdt.cpp
    PUBLIC_DEPS  noted::engine
)

if(NOTED_BUILD_TESTS)
    add_subdirectory(tests)
endif()
