#pragma once

// Workspace — full-viewport ImGui DockSpace coordinator.
//
// Photoshop / Procreate / Goodnotes all open with a sensible default
// panel arrangement (Layers + Brush on the right, navigator bottom-
// right, tools floating top-centre) and let the user customise from
// there. ImGui's DockSpace is the equivalent primitive; this widget
// sets it up at the start of every frame so every panel widget
// (`brush_options`, `layer_panel`, `navigator_panel`, `outline_panel`,
// `page_strip`, `color_picker_panel`, `status_bar`) auto-docks into
// the surrounding dock nodes instead of floating arbitrarily.
//
// The central dock node is **pass-through transparent** — clicks land
// on the underlying Vulkan canvas, so docking the panels doesn't
// steal pointer events from the drawing surface.
//
// On first launch (no `imgui.ini` yet) — or when the user explicitly
// asks for a layout reset via the `reset_layout` flag — the builder
// programmatically partitions the dock tree into named slots and
// pins each panel's window title to its home slot.  Subsequent
// launches load the user's saved layout from `imgui.ini` without
// disturbing customisations.
//
// Pure ImGui — no engine / domain dependencies. The caller (UiPanels)
// invokes `workspace_begin()` once per frame BEFORE drawing any
// docked panel.

namespace noted::ui::widget {

struct WorkspaceState {
    // First-frame guard: stays true until the builder has run once
    // in this process.  Toggle back to true to force a layout reset
    // from a "View → Reset Layout" menu item.
    bool needs_layout_build{true};
    // Set by `workspace_begin` after a successful rebuild — used by
    // the host's menu code (future Reset Layout / About) to surface
    // "Layout v3 loaded" telemetry. Not load-bearing.
    int active_layout_version{0};
};

// Bump this **whenever the structural dock layout in workspace.cpp
// changes** (split topology, panel→node assignment, or window
// titles). The persisted sidecar file (`<exe_dir>/imgui.layout.version`)
// stores the last-written value; on startup `workspace_begin` reads
// it and forces a full rebuild when it differs from the current code
// constant — guaranteeing the new layout takes effect immediately
// for every user without manual `imgui.ini` deletion.
//
// Increment policy:
//   - Only the structural code in `workspace.cpp::build_default_layout`
//     warrants a bump; per-panel rename / re-styling that doesn't move
//     a window between nodes does NOT.
//   - Bumps are sequential — never reuse a retired version number,
//     since old install bases may carry it on disk.
inline constexpr int kDockLayoutVersion = 8;

// Render the full-viewport DockSpace and (on first frame or after
// `state.needs_layout_build` is set) the programmatic default
// layout. Safe to call every frame — the actual layout build is
// gated by `needs_layout_build` and a runtime check on the ImGui
// dock node state, so the user's customisations survive across
// frames once the initial seed is laid down.
//
// Call BEFORE drawing any panel widget — DockBuilder needs the
// node tree set up before panels register themselves into it.
void workspace_begin(WorkspaceState& state);

}  // namespace noted::ui::widget
