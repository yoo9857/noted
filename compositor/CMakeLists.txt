noted_add_module(
    NAME    compositor
    SOURCES
        src/layer_compositor.cpp
        src/selection_rasterizer.cpp
        src/image_asset_gpu_registry.cpp
    PUBLIC_DEPS
        noted::engine
        noted::domain
        noted::platform
        imgui
)
