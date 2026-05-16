#include "noted/engine/gpu/instance.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "noted/engine/hook/registry.hpp"

namespace noted::gpu {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

[[nodiscard]] auto validation_layer_available() -> bool {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkLayerProperties> props(count);
    if (vkEnumerateInstanceLayerProperties(&count, props.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& p : props) {
        if (std::strcmp(p.layerName, kValidationLayer) == 0) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR auto VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                          VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                          void* /*user*/) -> VkBool32 {
    const bool severe = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool warn = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
    if (severe || warn) {
        noted::hook::registry().on_error.publish(noted::hook::ErrorObserved{
            .error = noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                data != nullptr && data->pMessage != nullptr ? data->pMessage : "<no message>"),
            .recoverable = !severe,
        });
    }
    return VK_FALSE;
}

[[nodiscard]] auto pfn_create_debug_messenger(VkInstance instance)
    -> PFN_vkCreateDebugUtilsMessengerEXT {
    return reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
}

[[nodiscard]] auto pfn_destroy_debug_messenger(VkInstance instance)
    -> PFN_vkDestroyDebugUtilsMessengerEXT {
    return reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
}

}  // namespace

auto Instance::create(const InstanceCreateInfo& info) -> Result<Instance> {
    const bool want_validation = info.enable_validation && validation_layer_available();

    std::vector<const char*> exts;
    exts.reserve(info.surface_extensions.size() + info.extra_extensions.size() + 1);
    for (auto* e : info.surface_extensions) {
        exts.push_back(e);
    }
    for (auto* e : info.extra_extensions) {
        exts.push_back(e);
    }
    if (want_validation) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const std::string app_name{info.app_name};

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = app_name.c_str();
    app.applicationVersion = info.app_version;
    app.pEngineName = "noted-engine";
    app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion = info.api_version;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.empty() ? nullptr : exts.data();
    if (want_validation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &kValidationLayer;
    }

    VkInstance handle = VK_NULL_HANDLE;
    if (auto vr = vkCreateInstance(&ci, nullptr, &handle); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(noted::ErrorCode::gpu_validation_failed,
                                                 std::string{"vkCreateInstance failed: VkResult="} +
                                                     std::to_string(static_cast<int>(vr))));
    }

    Instance out;
    out.handle_ = handle;

    if (want_validation) {
        auto pfn = pfn_create_debug_messenger(handle);
        if (pfn != nullptr) {
            VkDebugUtilsMessengerCreateInfoEXT mci{};
            mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mci.pfnUserCallback = debug_callback;
            (void) pfn(handle, &mci, nullptr, &out.debug_);
        }
    }
    return out;
}

Instance::Instance(Instance&& other) noexcept : handle_(other.handle_), debug_(other.debug_) {
    other.handle_ = VK_NULL_HANDLE;
    other.debug_ = VK_NULL_HANDLE;
}

auto Instance::operator=(Instance&& other) noexcept -> Instance& {
    if (this != &other) {
        destroy();
        handle_ = other.handle_;
        debug_ = other.debug_;
        other.handle_ = VK_NULL_HANDLE;
        other.debug_ = VK_NULL_HANDLE;
    }
    return *this;
}

Instance::~Instance() {
    destroy();
}

void Instance::destroy() noexcept {
    if (debug_ != VK_NULL_HANDLE && handle_ != VK_NULL_HANDLE) {
        if (auto pfn = pfn_destroy_debug_messenger(handle_); pfn != nullptr) {
            pfn(handle_, debug_, nullptr);
        }
        debug_ = VK_NULL_HANDLE;
    }
    if (handle_ != VK_NULL_HANDLE) {
        vkDestroyInstance(handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

}  // namespace noted::gpu
