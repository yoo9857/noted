noted_add_module(
    NAME    compositor
    SOURCES
        src/layer_compositor.cpp
    PUBLIC_DEPS
        noted::engine
        noted::domain
)
