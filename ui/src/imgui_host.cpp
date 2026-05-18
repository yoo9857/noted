#include "noted/ui/imgui_host.hpp"

#include <array>
#include <cstring>
#include <string>
#include <utility>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"

namespace noted::ui {

namespace {

// Pool size matches Dear ImGui's official sample — one descriptor of
// each common type, 1000 max sets. The atlas + dynamic widget state
// fits well under this; the over-allocation is ~tens of KB and pays
// for itself by absorbing any future widget that grows descriptor
// usage without forcing a pool rebuild.
constexpr std::uint32_t kImguiPoolMaxSets = 1000;
constexpr std::array<VkDescriptorPoolSize, 11> kImguiPoolSizes{{
    {VK_DESCRIPTOR_TYPE_SAMPLER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kImguiPoolMaxSets},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, kImguiPoolMaxSets},
}};

// VkResult callback ImGui invokes for every Vulkan call it makes.
// We pump through harness::validate at the call site rather than here
// — see ADR 0027 follow-up. For now log to stderr if non-success;
// keeps the failure visible without crashing the frame.
void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS) {
        return;
    }
    // The implementation file does not include <iostream> intentionally
    // — that lives in the app entry point. A future PR routes this
    // through harness::validate; until then keep it noisy via the
    // standard C runtime.
    std::fprintf(stderr, "ImGui Vulkan call failed: VkResult=%d\n", static_cast<int>(err));
}

}  // namespace

auto ImGuiHost::create(const ImGuiHostCreateInfo& info) -> Result<ImGuiHost> {
    if (info.instance == nullptr || info.physical_device == nullptr || info.device == nullptr ||
        info.window == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "ImGuiHost::create: instance / physical_device / device / "
                              "window must all be non-null"));
    }
    if (info.color_format == VK_FORMAT_UNDEFINED) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "ImGuiHost::create: color_format must not be VK_FORMAT_UNDEFINED"));
    }
    if (info.image_count < 2 || info.image_count > 16) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "ImGuiHost::create: image_count " +
                                                     std::to_string(info.image_count) +
                                                     " is outside the supported [2, 16] range"));
    }

    ImGuiHost host{};
    host.device_ = info.device->handle();

    // 1. Descriptor pool for ImGui's font texture + per-frame state.
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = kImguiPoolMaxSets * static_cast<std::uint32_t>(kImguiPoolSizes.size());
    pool_info.poolSizeCount = static_cast<std::uint32_t>(kImguiPoolSizes.size());
    pool_info.pPoolSizes = kImguiPoolSizes.data();
    if (auto vr = vkCreateDescriptorPool(host.device_, &pool_info, nullptr, &host.descriptor_pool_);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_out_of_memory,
            std::string{"ImGuiHost::create: vkCreateDescriptorPool failed: VkResult="} +
                std::to_string(static_cast<int>(vr))));
    }

    // 2. ImGui context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    host.context_owned_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Multi-viewport is intentionally off: it spawns OS-level child
    // windows that conflict with our pen-input subclass on Windows
    // (ADR 0017). Re-enable if the docking refactor establishes a
    // clean child-window pen-input path.

    // 3. GLFW backend — wires input and clipboard.
    if (!ImGui_ImplGlfw_InitForVulkan(info.window, /*install_callbacks=*/true)) {
        host.destroy();
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "ImGuiHost::create: ImGui_ImplGlfw_InitForVulkan returned false"));
    }
    host.glfw_init_ = true;

    // 4. Vulkan backend. We use dynamic rendering so no VkRenderPass.
    VkFormat color_formats[1] = {info.color_format};
    VkPipelineRenderingCreateInfo pipeline_rendering{};
    pipeline_rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering.colorAttachmentCount = 1;
    pipeline_rendering.pColorAttachmentFormats = color_formats;

    ImGui_ImplVulkan_InitInfo vk_info{};
    vk_info.Instance = info.instance->handle();
    vk_info.PhysicalDevice = info.physical_device->handle();
    vk_info.Device = host.device_;
    vk_info.QueueFamily = info.device->graphics_family();
    vk_info.Queue = info.device->graphics_queue();
    vk_info.DescriptorPool = host.descriptor_pool_;
    vk_info.MinImageCount = info.image_count;
    vk_info.ImageCount = info.image_count;
    vk_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vk_info.PipelineCache = VK_NULL_HANDLE;
    vk_info.Allocator = nullptr;
    vk_info.CheckVkResultFn = check_vk_result;
    vk_info.UseDynamicRendering = true;
    vk_info.PipelineRenderingCreateInfo = pipeline_rendering;

    if (!ImGui_ImplVulkan_Init(&vk_info)) {
        host.destroy();
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state,
                              "ImGuiHost::create: ImGui_ImplVulkan_Init returned false"));
    }
    host.vulkan_init_ = true;
    host.initialized_ = true;
    // Font texture is uploaded lazily on the first frame in ImGui
    // 1.91+; no manual upload step needed.
    return host;
}

ImGuiHost::ImGuiHost(ImGuiHost&& other) noexcept
    : device_(other.device_),
      descriptor_pool_(other.descriptor_pool_),
      context_owned_(other.context_owned_),
      glfw_init_(other.glfw_init_),
      vulkan_init_(other.vulkan_init_),
      initialized_(other.initialized_) {
    other.device_ = VK_NULL_HANDLE;
    other.descriptor_pool_ = VK_NULL_HANDLE;
    other.context_owned_ = false;
    other.glfw_init_ = false;
    other.vulkan_init_ = false;
    other.initialized_ = false;
}

auto ImGuiHost::operator=(ImGuiHost&& other) noexcept -> ImGuiHost& {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        descriptor_pool_ = other.descriptor_pool_;
        context_owned_ = other.context_owned_;
        glfw_init_ = other.glfw_init_;
        vulkan_init_ = other.vulkan_init_;
        initialized_ = other.initialized_;
        other.device_ = VK_NULL_HANDLE;
        other.descriptor_pool_ = VK_NULL_HANDLE;
        other.context_owned_ = false;
        other.glfw_init_ = false;
        other.vulkan_init_ = false;
        other.initialized_ = false;
    }
    return *this;
}

ImGuiHost::~ImGuiHost() {
    destroy();
}

void ImGuiHost::destroy() noexcept {
    if (vulkan_init_) {
        ImGui_ImplVulkan_Shutdown();
        vulkan_init_ = false;
    }
    if (glfw_init_) {
        ImGui_ImplGlfw_Shutdown();
        glfw_init_ = false;
    }
    if (context_owned_) {
        ImGui::DestroyContext();
        context_owned_ = false;
    }
    if (descriptor_pool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    }
    descriptor_pool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    initialized_ = false;
}

void ImGuiHost::begin_frame() noexcept {
    if (!initialized_) {
        return;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiHost::finalize_frame() noexcept {
    if (!initialized_) {
        return;
    }
    // Produces draw data and ends the frame. Decoupled from
    // render_into so a frame can be cleanly finalized even when
    // rendering is aborted (swapchain out-of-date, etc.).
    ImGui::Render();
}

void ImGuiHost::render_into(VkCommandBuffer cb) noexcept {
    if (!initialized_) {
        return;
    }
    if (auto* draw_data = ImGui::GetDrawData(); draw_data != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, cb);
    }
}

}  // namespace noted::ui
