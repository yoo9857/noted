#include "noted/engine/gpu/shader_module.hpp"

#include <string>

namespace noted::gpu {

auto ShaderModule::create(const Device& device,
                          std::span<const std::uint32_t> spirv) -> Result<ShaderModule> {
    if (spirv.empty()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "ShaderModule::create: empty SPIR-V span"));
    }

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size_bytes();
    ci.pCode = spirv.data();

    VkShaderModule raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateShaderModule(device.handle(), &ci, nullptr, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_shader_compile_failed,
            std::string{"vkCreateShaderModule: "} + std::to_string(static_cast<int>(vr))));
    }
    ShaderModule m;
    m.owner_ = device.handle();
    m.handle_ = raw;
    return m;
}

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto ShaderModule::operator=(ShaderModule&& other) noexcept -> ShaderModule& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        handle_ = other.handle_;
        other.owner_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

ShaderModule::~ShaderModule() {
    destroy();
}

void ShaderModule::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_ = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
