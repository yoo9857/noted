#include "noted/ui/widget/workspace.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

#include <imgui.h>
#include <imgui_internal.h>  // ImGui::DockBuilder*

#include "noted/platform/fs/fs.hpp"

namespace noted::ui::widget {

namespace {

// Window titles each panel calls `ImGui::Begin` with. The dock
// builder uses these to pin panels to their home node — they MUST
// match the strings the panel widgets pass to `ImGui::Begin` exactly
// (case + whitespace). Keeping them as inline constexpr string-views
// next to the layout code makes the contract obvious and a future
// rename one-stop.
constexpr const char* kDockLayers = "Layers";
constexpr const char* kDockBrush = "Brush options";
constexpr const char* kDockColors = "Color";
constexpr const char* kDockPages = "Pages";
constexpr const char* kDockOutline = "Outline";
constexpr const char* kDockNavigator = "Navigator";

// Sidecar filename used to persist the layout version. Sits next to
// `imgui.ini` in the executable directory so wiping one wipes both
// in lockstep — `rm <exe_dir>/imgui*` is the user-facing nuke for
// "I want a fresh layout."
constexpr const char* kLayoutVersionFile = "imgui.layout.version";

[[nodiscard]] auto version_file_path() -> std::filesystem::path {
    const auto exe = noted::platform::fs::executable_dir();
    if (exe.empty()) {
        return {};
    }
    return exe / kLayoutVersionFile;
}

[[nodiscard]] auto read_saved_layout_version() -> int {
    const auto path = version_file_path();
    if (path.empty()) {
        return -1;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return -1;
    }
    std::ifstream in{path};
    int v = -1;
    in >> v;
    return in ? v : -1;
}

void write_saved_layout_version(int version) noexcept {
    const auto path = version_file_path();
    if (path.empty()) {
        return;
    }
    // Best-effort write; we don't fail the frame if the FS is read-
    // only or the disk is full. The cost of a missed write is just
    // a redundant rebuild on the next launch — not a correctness bug.
    try {
        std::ofstream out{path, std::ios::trunc};
        out << version << '\n';
    } catch (...) {
        // swallow — see rationale above
    }
}

void build_default_layout(ImGuiID dockspace_id, ImVec2 size) {
    // Wipe any existing layout on this dockspace id and seed our own.
    // Forced on a layout-version bump; otherwise gated by the
    // caller's empty-node check so the user's customised layout
    // survives.
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        static_cast<ImGuiDockNodeFlags>(static_cast<int>(ImGuiDockNodeFlags_DockSpace) |
                                        static_cast<int>(ImGuiDockNodeFlags_PassthruCentralNode)));
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    // Layout grammar — v6, paint controls on the left / document
    // structure on the right:
    //   - LEFT column = **paint controls**, stacked top→bottom:
    //       Top:    Color (HSV + 5×4 palette + numeric inputs).
    //       Bottom: Brush options (size / pressure / stabilizer).
    //   - RIGHT column = **document structure**:
    //       Top:    Layers | Pages | Outline (tabbed, Layers first).
    //       Bottom: Navigator overview map.
    //   - Centre = transparent passthrough — the Vulkan canvas
    //     receives every pointer event that doesn't hit a panel.
    //
    // Stacking paint controls on the left and structure controls
    // on the right matches the Procreate / Photoshop conventions
    // most artists already train on. Proportions tuned for a
    // 1600×1000 reference window; ImGui clamps to the visible area
    // so smaller windows shrink the sides smoothly.
    ImGuiID right_id = 0;
    ImGuiID centre_id = 0;
    ImGui::DockBuilderSplitNode(
        dockspace_id, ImGuiDir_Right, /*size_ratio=*/0.22F, &right_id, &centre_id);

    ImGuiID left_id = 0;
    ImGui::DockBuilderSplitNode(
        centre_id, ImGuiDir_Left, /*size_ratio=*/0.22F, &left_id, &centre_id);

    // Left column: Color (top ~65%) + Brush (bottom ~35%). Color
    // needs the height for the HSV wheel + 5×4 palette tray; Brush
    // is just a handful of compact slider rows.
    ImGuiID left_brush_id = 0;
    ImGuiID left_color_id = 0;
    ImGui::DockBuilderSplitNode(
        left_id, ImGuiDir_Down, /*size_ratio=*/0.35F, &left_brush_id, &left_color_id);

    // Right column: Layers / Pages / Outline tabs on top (~80%),
    // Navigator below (~20%). Navigator is a small thumbnail map —
    // it doesn't need the height the structure tabs do (multi-row
    // layer list, page list, outline tree). At 1000-px window
    // height that's ~200 px for the Navigator, comfortably
    // accommodating a thumbnail proportional to the canvas while
    // leaving the structure tabs ~800 px to breathe.
    ImGuiID right_navigator_id = 0;
    ImGuiID right_structure_id = 0;
    ImGui::DockBuilderSplitNode(
        right_id, ImGuiDir_Down, /*size_ratio=*/0.20F, &right_navigator_id, &right_structure_id);

    // Pin each panel. Tab order inside `right_structure_id` matches
    // the call order: Layers leads, then Pages, then Outline.
    ImGui::DockBuilderDockWindow(kDockColors, left_color_id);
    ImGui::DockBuilderDockWindow(kDockBrush, left_brush_id);
    ImGui::DockBuilderDockWindow(kDockLayers, right_structure_id);
    ImGui::DockBuilderDockWindow(kDockPages, right_structure_id);
    ImGui::DockBuilderDockWindow(kDockOutline, right_structure_id);
    ImGui::DockBuilderDockWindow(kDockNavigator, right_navigator_id);

    ImGui::DockBuilderFinish(dockspace_id);
}

}  // namespace

void workspace_begin(WorkspaceState& state) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr) {
        return;
    }

    // The dockspace host is a full-viewport invisible window. The
    // flag set strips chrome / borders / move / resize so it really
    // is just a layout container — the visible chrome (panels, the
    // 12-o'clock toolbar) draws on top of it without disrupting the
    // dock tree.
    ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("##noted_workspace", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID("##noted_dockspace");
    // PassthruCentralNode keeps the central area transparent so pointer
    // events fall through to the underlying Vulkan canvas — the user
    // can pan / zoom / draw without docking interception.
    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_PassthruCentralNode);

    if (state.needs_layout_build) {
        // Versioned rebuild — the developer bumps `kDockLayoutVersion`
        // whenever they change `build_default_layout`; the sidecar
        // file holds the version last persisted. Mismatch → force a
        // rebuild regardless of the current node state, overriding
        // whatever stale node-id tree the user's `imgui.ini` was
        // carrying. Match → only rebuild if the dock is empty (fresh
        // install / deleted ini), which preserves the user's
        // customisations.
        const int saved_version = read_saved_layout_version();
        const bool version_mismatch = (saved_version != kDockLayoutVersion);
        const auto* existing = ImGui::DockBuilderGetNode(dockspace_id);
        const bool empty_node = (existing == nullptr) || (existing->IsEmpty());
        if (version_mismatch || empty_node) {
            build_default_layout(dockspace_id, vp->WorkSize);
            write_saved_layout_version(kDockLayoutVersion);
        }
        state.active_layout_version = kDockLayoutVersion;
        state.needs_layout_build = false;
    }

    ImGui::End();
}

}  // namespace noted::ui::widget
