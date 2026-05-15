noted_add_module(
    NAME    engine
    SOURCES
        src/engine.cpp
        src/hook/registry.cpp
        src/error/error.cpp
        src/harness/harness.cpp
        src/gpu/gpu.cpp
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
        src/gpu/upload.cpp
        src/gpu/sampler.cpp
        src/gpu/shader_module.cpp
        src/gpu/descriptor_set_layout.cpp
        src/gpu/descriptor_pool.cpp
        src/gpu/descriptor_set.cpp
        src/gpu/pipeline_layout.cpp
        src/gpu/graphics_pipeline.cpp
        src/tile/tile.cpp
        src/color/color.cpp
        src/job/job.cpp
        src/memory/memory.cpp
    PUBLIC_DEPS
        Vulkan::Vulkan
        glfw
        VulkanMemoryAllocator
)

# Module-local tests live under //tests/unit (shared with other modules).
# When the engine grows GPU integration tests, this is where they slot in.
