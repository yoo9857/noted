noted_add_module(
    NAME    engine
    SOURCES
        src/engine.cpp
        src/hook/registry.cpp
        src/error/error.cpp
        src/harness/harness.cpp
        src/gpu/instance.cpp
        src/gpu/physical_device.cpp
        src/gpu/surface.cpp
        src/gpu/device.cpp
        src/gpu/swapchain.cpp
        src/gpu/command_pool.cpp
        src/gpu/command_buffer.cpp
        src/gpu/frame_sync.cpp
        src/gpu/renderer.cpp
        src/gpu/allocator.cpp
        src/gpu/buffer.cpp
        src/gpu/image.cpp
        src/gpu/canvas_render_target.cpp
        src/gpu/selection_mask.cpp
        src/gpu/upload.cpp
        src/gpu/immediate_submit.cpp
        src/gpu/sampler.cpp
        src/gpu/shader_module.cpp
        src/gpu/descriptor_set_layout.cpp
        src/gpu/descriptor_pool.cpp
        src/gpu/descriptor_set.cpp
        src/gpu/pipeline_layout.cpp
        src/gpu/graphics_pipeline.cpp
        src/stroke/stroke_engine.cpp
        src/stroke/stroke_geometry.cpp
        src/canvas/camera.cpp
        src/canvas/page.cpp
        src/canvas/page_renderer.cpp
    PUBLIC_DEPS
        Vulkan::Vulkan
        glfw
        VulkanMemoryAllocator
)

# Tracy — optional, build-time flag NOTED_ENABLE_TRACY.
# TracyClient is linked as a normal (object/static) library produced by
# Tracy's CMakeLists. We propagate the macro NOTED_TRACY_ENABLED publicly
# so headers that gate on it (noted/engine/profile.hpp) see the same value
# in every consumer of the engine.
if(NOTED_ENABLE_TRACY)
    target_link_libraries(noted_engine PUBLIC Tracy::TracyClient)
    target_compile_definitions(noted_engine PUBLIC NOTED_TRACY_ENABLED=1)
endif()

# Module-local tests live under //tests/unit (shared with other modules).
# When the engine grows GPU integration tests, this is where they slot in.
