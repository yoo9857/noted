noted_add_module(
    NAME    plugin
    SOURCES
        src/host/host.cpp
        src/sandbox/sandbox.cpp
        src/api/api.cpp
    PUBLIC_DEPS  noted::engine
)
