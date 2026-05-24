#include "noted/engine/gpu/device.hpp"

#include <array>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace noted::gpu {

namespace {

constexpr const char* kSwapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

[[nodiscard]] auto find_present_family(VkPhysicalDevice physical,
                                       VkSurfaceKHR surface,
                                       std::uint32_t preferred) -> std::uint32_t {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);

    // Prefer the graphics family if it supports present.
    if (preferred != UINT32_MAX) {
        VkBool32 supported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physical, preferred, surface, &supported) ==
                VK_SUCCESS &&
            supported == VK_TRUE) {
            return preferred;
        }
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        VkBool32 supported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &supported) == VK_SUCCESS &&
            supported == VK_TRUE) {
            return i;
        }
    }
    return UINT32_MAX;
}

}  // namespace

auto Device::create(const PhysicalDevice& physical,
                    const Surface& surface,
                    const DeviceCreateInfo& info) -> Result<Device> {
    const auto graphics_fam = physical.queue_families().graphics;
    const auto present_fam = find_present_family(physical.handle(), surface.handle(), graphics_fam);
    if (present_fam == UINT32_MAX) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_validation_failed,
                              "no queue family on the chosen physical device supports presenting "
                              "to the given surface"));
    }

    const std::set<std::uint32_t> unique_families{graphics_fam, present_fam};
    constexpr float kPriority = 1.0F;

    std::vector<VkDeviceQueueCreateInfo> qcis;
    qcis.reserve(unique_families.size());
    for (auto fam : unique_families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &kPriority;
        qcis.push_back(qci);
    }

    std::vector<const char*> exts;
    exts.reserve(info.extra_extensions.size() + 1);
    exts.push_back(kSwapchainExt);
    for (auto* e : info.extra_extensions) {
        // Skip duplicates of the swapchain extension that callers might
        // have included themselves.
        if (std::strcmp(e, kSwapchainExt) != 0) {
            exts.push_back(e);
        }
    }

    // 1.3 features (dynamic rendering, sync2).
    VkPhysicalDeviceVulkan13Features vk13{};
    vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13.dynamicRendering = info.enable_dynamic_rendering ? VK_TRUE : VK_FALSE;
    vk13.synchronization2 = info.enable_synchronization2 ? VK_TRUE : VK_FALSE;

    // 1.1 features the engine relies on. shaderDrawParameters is what
    // exposes SV_VertexID / gl_VertexIndex to vertex shaders.
    VkPhysicalDeviceVulkan11Features vk11{};
    vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vk11.pNext = &vk13;
    vk11.shaderDrawParameters = info.enable_shader_draw_parameters ? VK_TRUE : VK_FALSE;

    // 1.2 features: the 2026 baseline — descriptor indexing for bindless,
    // buffer device address for pointer-as-uniform shader code, timeline
    // semaphores as the canonical multi-queue sync primitive.
    VkPhysicalDeviceVulkan12Features vk12{};
    vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12.pNext = &vk11;
    if (info.enable_descriptor_indexing) {
        vk12.descriptorIndexing = VK_TRUE;
        vk12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vk12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        vk12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vk12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        vk12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        vk12.descriptorBindingPartiallyBound = VK_TRUE;
        vk12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vk12.runtimeDescriptorArray = VK_TRUE;
    }
    if (info.enable_buffer_device_address) {
        vk12.bufferDeviceAddress = VK_TRUE;
    }
    if (info.enable_timeline_semaphore) {
        vk12.timelineSemaphore = VK_TRUE;
    }

    // ---- Vulkan 1.4 optional features ----
    // dynamicRenderingLocalRead is the core-1.4 promotion of
    // VK_KHR_dynamic_rendering_local_read. We query first and only
    // chain the struct (with the bit set) when the physical device
    // advertises support; older devices skip it gracefully and the
    // compositor falls back to NORMAL for the 12 shader-blend modes.
    VkPhysicalDeviceVulkan14Features vk14{};
    vk14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    bool local_read_supported = false;
    if (info.enable_dynamic_rendering_local_read) {
        VkPhysicalDeviceVulkan14Features query{};
        query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
        VkPhysicalDeviceFeatures2 query_feat2{};
        query_feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        query_feat2.pNext = &query;
        vkGetPhysicalDeviceFeatures2(physical.handle(), &query_feat2);
        local_read_supported = (query.dynamicRenderingLocalRead == VK_TRUE);
        if (local_read_supported) {
            vk14.dynamicRenderingLocalRead = VK_TRUE;
            vk14.pNext = &vk12;
        }
    }

    VkPhysicalDeviceFeatures2 feat2{};
    feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    feat2.pNext = local_read_supported ? static_cast<void*>(&vk14) : static_cast<void*>(&vk12);

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &feat2;
    dci.queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size());
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    dci.ppEnabledExtensionNames = exts.data();

    VkDevice raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateDevice(physical.handle(), &dci, nullptr, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(noted::ErrorCode::gpu_validation_failed,
                                                 std::string{"vkCreateDevice failed: VkResult="} +
                                                     std::to_string(static_cast<int>(vr))));
    }

    Device out;
    out.handle_ = raw;
    out.graphics_family_ = graphics_fam;
    out.present_family_ = present_fam;
    out.has_dynamic_rendering_local_read_ = local_read_supported;
    vkGetDeviceQueue(raw, graphics_fam, 0, &out.graphics_queue_);
    vkGetDeviceQueue(raw, present_fam, 0, &out.present_queue_);
    return out;
}

Device::Device(Device&& other) noexcept
    : handle_(other.handle_),
      graphics_queue_(other.graphics_queue_),
      present_queue_(other.present_queue_),
      graphics_family_(other.graphics_family_),
      present_family_(other.present_family_),
      has_dynamic_rendering_local_read_(other.has_dynamic_rendering_local_read_) {
    other.handle_ = VK_NULL_HANDLE;
    other.graphics_queue_ = VK_NULL_HANDLE;
    other.present_queue_ = VK_NULL_HANDLE;
    other.graphics_family_ = UINT32_MAX;
    other.present_family_ = UINT32_MAX;
    other.has_dynamic_rendering_local_read_ = false;
}

auto Device::operator=(Device&& other) noexcept -> Device& {
    if (this != &other) {
        destroy();
        handle_ = other.handle_;
        graphics_queue_ = other.graphics_queue_;
        present_queue_ = other.present_queue_;
        graphics_family_ = other.graphics_family_;
        present_family_ = other.present_family_;
        has_dynamic_rendering_local_read_ = other.has_dynamic_rendering_local_read_;
        other.handle_ = VK_NULL_HANDLE;
        other.graphics_queue_ = VK_NULL_HANDLE;
        other.present_queue_ = VK_NULL_HANDLE;
        other.graphics_family_ = UINT32_MAX;
        other.present_family_ = UINT32_MAX;
        other.has_dynamic_rendering_local_read_ = false;
    }
    return *this;
}

Device::~Device() {
    destroy();
}

void Device::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        // No use-after-free on outstanding work: callers are expected to
        // wait_idle before letting the Device go out of scope.
        vkDestroyDevice(handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

void Device::wait_idle() const noexcept {
    if (handle_ != VK_NULL_HANDLE) {
        (void) vkDeviceWaitIdle(handle_);
    }
}

}  // namespace noted::gpu
