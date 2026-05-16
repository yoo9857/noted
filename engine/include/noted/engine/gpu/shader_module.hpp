#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// VkShaderModule RAII wrapper.
//
// Accepts SPIR-V as a span of 32-bit words. Source compilation (GLSL /
// HLSL / Slang → SPIR-V) is out of scope here; callers either bake SPIR-V
// into the binary at build time (the engine's default) or compile at
// runtime via the future feat/shader-compile path.
class ShaderModule {
public:
    [[nodiscard]] static auto create(const Device& device,
                                     std::span<const std::uint32_t> spirv) -> Result<ShaderModule>;

    ShaderModule(ShaderModule&& other) noexcept;
    auto operator=(ShaderModule&& other) noexcept -> ShaderModule&;
    ShaderModule(const ShaderModule&) = delete;
    auto operator=(const ShaderModule&) -> ShaderModule& = delete;
    ~ShaderModule();

    [[nodiscard]] auto handle() const noexcept -> VkShaderModule { return handle_; }

private:
    ShaderModule() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkShaderModule handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
