#include "noted/ui/imgui_host.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
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

// Try to load the caller-supplied font with the Korean glyph range
// merged in. On any failure (missing file, parse error, atlas build
// failure) emit a warning and fall back to the default ProggyClean
// font — a missing CJK font is degraded UX, not a fatal error
// (HANDOFF / ADR 0027 resilience posture).
//
// Returns true when the custom font took effect, false on fallback.
// The caller (create()) does not act on the return value beyond
// logging — both paths produce a usable atlas.
[[nodiscard]] auto try_load_cjk_font(const std::filesystem::path& path, float size_px) -> bool {
    ImGuiIO& io = ImGui::GetIO();
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        std::fprintf(stderr,
                     "ImGuiHost: CJK font '%s' not found; falling back to default\n",
                     path.string().c_str());
        return false;
    }
    if (!(size_px > 0.0F)) {
        std::fprintf(stderr,
                     "ImGuiHost: CJK font size_px=%.2f is non-positive; falling back to default\n",
                     static_cast<double>(size_px));
        return false;
    }

    // A larger atlas is required to hold the 2350+ Hangul Syllables
    // plus Latin basic + ASCII at a legible 16+ px size. 2048×2048 is
    // the standard "enough headroom for KR" size in ImGui's own
    // examples; the atlas allocation is ~4 MB once.
    io.Fonts->TexDesiredWidth = 2048;

    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = false;

    // GetGlyphRangesKorean() returns ranges that cover Latin basic +
    // Hangul Syllables + Hangul Jamo. Sufficient for the user's
    // primary language; an extended-CJK build can swap to a merged
    // KR+JP+CN range in a follow-up if Japanese/Chinese typing lands.
    const ImWchar* ranges = io.Fonts->GetGlyphRangesKorean();

    // The file content is owned by ImGui after AddFontFromFileTTF —
    // it reads the bytes and keeps them. ImGui's loader returns nullptr
    // on parse failure; we don't pre-clear the atlas because the
    // default font is added on demand if no font has been registered.
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size_px, &cfg, ranges);
    if (font == nullptr) {
        std::fprintf(stderr,
                     "ImGuiHost: AddFontFromFileTTF('%s', %.1f) returned null; "
                     "falling back to default\n",
                     path.string().c_str(),
                     static_cast<double>(size_px));
        // Clear any partially-added state so the default-font path is
        // pristine. Build() below would otherwise see a half-registered
        // entry.
        io.Fonts->Clear();
        return false;
    }
    if (!io.Fonts->Build()) {
        std::fprintf(stderr,
                     "ImGuiHost: ImFontAtlas::Build() failed for '%s'; falling back to default\n",
                     path.string().c_str());
        io.Fonts->Clear();
        return false;
    }
    return true;
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

    // 2a. Font atlas. Wire the optional CJK font BEFORE the Vulkan
    // backend init so ImGui_ImplVulkan_Init's lazy font-texture upload
    // sees the final atlas. The helper falls back to the default
    // bitmap font on any failure — never blocks create().
    (void) try_load_cjk_font(info.cjk_font_path, info.font_size_px);

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
