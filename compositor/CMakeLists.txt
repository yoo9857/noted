noted_add_module(
    NAME    compositor
    SOURCES
        src/layer_compositor.cpp
        src/selection_rasterizer.cpp
    PUBLIC_DEPS
        noted::engine
        noted::domain
)
