#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"

namespace noted::gpu {

struct InstanceCreateInfo {
    std::string_view             app_name           = "noted";
    std::uint32_t                app_version        = 0;
    std::uint32_t                api_version        = VK_API_VERSION_1_3;
    bool                         enable_validation  = false;
    // Extra extensions beyond the platform/window surface set the platform
    // layer asks for.
    std::span<const char* const> extra_extensions   = {};
    std::span<const char* const> surface_extensions = {};
};

// RAII Vulkan instance.
//
// On construction, requests VK_EXT_debug_utils when validation is enabled
// and installs a messenger that publishes severe messages to hook::on_error.
// Move-only; no copy, no shared ownership. Underlying VkInstance is destroyed
// in the destructor.
class Instance {
public:
    [[nodiscard]] static auto create(const InstanceCreateInfo& info) -> Result<Instance>;

    Instance(Instance&& other) noexcept;
    auto operator=(Instance&& other) noexcept -> Instance&;
    Instance(const Instance&) = delete;
    auto operator=(const Instance&) -> Instance& = delete;
    ~Instance();

    [[nodiscard]] auto handle() const noexcept -> VkInstance { return handle_; }
    [[nodiscard]] auto has_validation() const noexcept -> bool { return debug_ != VK_NULL_HANDLE; }

private:
    Instance() = default;
    void destroy() noexcept;

    VkInstance               handle_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_  = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
