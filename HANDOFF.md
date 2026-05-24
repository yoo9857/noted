# Handoff — where the project is and what's next

**Last updated:** 2026-05-24 · **main HEAD:** `87b0130` (clean, 0 open PRs)

Goal: a professional note-taking + raster image editor that exceeds
Goodnotes (vector ink, stylus-first) AND Photoshop (raster layers,
filters) in one unified document. AAA-grade performance, maintainability,
exception handling.

---

## Where we are

**The app builds, runs, and is interactive end-to-end** (mid v1.0
migration). A frameless 1600×1000 `QWindow` opens with Mac-style
chrome (traffic-light controls, vibrant titlebar). The GPU is
picked via `QVulkanInstance`, a `LayerCompositor` walks the active
document's `LayerGraph` (4 fixed-function blend modes + counted
fallback for the other 12), the stroke engine overlays vector-ink
polyline ribbons with **real pen-dynamics** (2-handle bezier
`PressureCurve`, velocity-tapered width, tilt-aware calligraphy
width, SDF soft-edge falloff), and a hybrid Qt-QML + Dear ImGui
product shell renders on top:

  - **12-o'clock floating toolbar** (QML, Phase 3 of ADR 0034) —
    Mac-grade icon strip anchored top-centre with hover blur.
  - **Side rails (still Dear ImGui — Phase 4 pending)**:

    - **Menu bar** (File / Edit / View / About). File's New / Open /
      Save / Save As back the `.noted` JSON+zip format via
      nativefiledialog-extended. Edit's Undo / Redo back the live
      `UndoStack`; Edit → Add Block emits `AddBlockCommand` for any
      of the 7 BlockKinds. Title bar shows filename + `*` dirty
      marker. Ctrl+N/O/S/Shift+S/Z/Y/Q chords. Dirty-confirm modal
      on close / New.
    - **Page strip** — left-rail. Mini preview of each page's
      actual pattern (grid / lined / dotted); add / remove via
      footer button + right-click menu. Click row → camera jumps.
    - **Tool palette** — pen / eraser / selection (rect) / lasso /
      shape (rect, ellipse) / text / image. Each tool snapshots
      its options at PRESS so toggling a tool never rewrites
      already-committed work.
    - **Brush options** — size sliders, opacity, **2-handle
      bezier pressure-curve editor**, velocity/tilt blend sliders,
      soft-edge `softness_ratio`. Live-edited every frame.
    - **Brush library picker** — AAA presets (pencil / ink pen /
      felt-tip / soft brush / charcoal / highlighter) + custom
      brush save/load (`~/.noted/brushes.json`).
    - **Floating colour picker** (right side, OpenCanvas-style) —
      swatches + HSV wheel + history strip.
    - **Layer panel** — visibility, opacity (slider-coalesced
      Command), blend mode, name; keyboard shortcuts
      Ctrl+Shift+N (new), Ctrl+J (duplicate), Delete (remove),
      Alt+]/[ (cycle active), Ctrl+E (merge down),
      Ctrl+Shift+E (merge visible). Every mutation is a Command
      on `UndoStack`.
    - **Navigator** — scaled-down canvas thumbnail with live
      viewport rect; drag the rect to pan camera.
    - **Outline panel** — tree of `Document.preorder` with
      click-to-select + F2 inline rename.
    - **Status bar** — frame index + FPS + zoom % + **active
      layer name** pinned to bottom.
    - **Debug overlay** (View → Debug overlay; off by default) —
      frame + FPS, 120-sample CPU plot, `LayerCompositor`
      fallback count, every `harness::Counter`.

Pages render on a single neutral desk fill (ADR 0033), each with
a soft drop shadow + AA edges — no canvas-margin band under zoom
out. Smart-shape recognizer (PR #103) snaps drawn freehand into
ShapePrimitive when geometric + dwell heuristics agree.

The frame loop ticks at ~0.5 ms CPU on a GTX 1050 Ti (240-frame
sample) with **zero Vulkan validation errors** — the LayerCompositor
moved its one-time dummy-mask init out of the render pass and now
rotates descriptor sets per frame-in-flight (ADR 0028).
ImGui's `IM_ASSERT` routes through `harness::validate` so internal
invariant violations land in the same observability channel as
every other engine assertion (ADR 0027).

### Stack

- **C++23** (MSVC 17.10+, GCC 13+)
- **Vulkan 1.4** (SDK 1.4.309+)
  - 1.3 features: `dynamicRendering`, `synchronization2`
  - 1.2 features: descriptor indexing (bindless-ready),
    `bufferDeviceAddress`, `timelineSemaphore`
  - 1.1 features: `shaderDrawParameters`
- **Slang** (Microsoft + Khronos) as the only shader language
- **VMA** (AMD GPUOpen) for GPU memory
- **Qt 6.7+** (`QVulkanInstance` / `QWindow` / Qt Quick) — v1.0
  UI shell per ADR 0034. The 12-o'clock toolbar is already QML;
  side panels migrate in Phase 4.
- **stb_image** for PNG/JPG decode
- **nlohmann/json** for `.noted` document serialization
- **miniz** for the `.noted` zip container
- **Dear ImGui** (docking branch) — transitional side panels
  during the Qt migration window; expected to retire at
  ADR 0034 Phase 4 completion
- **nativefiledialog-extended** — native open/save dialogs and
  the "Pick image…" picker
- **CMake 3.28+** with `FetchContent` for deps
- **GoogleTest** for unit tests

### Build matrix (CI)

| OS | Compiler | Configs |
|---|---|---|
| Windows | MSVC 2022 | Debug, Release |
| Linux | g++-13 | Debug, Release |
| Linux | g++-13 + ASan + UBSan | Debug |

All checks block PRs. `clang-format-18` enforces the project style on
every push (see `.clang-format`, `.github/workflows/lint.yml`).
Conventional-commit subjects enforced by the `commit message lint`
job — pattern `^(feat|fix|chore|refactor|docs|test|perf|build|ci|style)(\([a-z0-9._-]+\))?!?: .+`.
Scopes must use only `[a-z0-9._-]` — no `+`, no spaces.

### Repo layout

```
engine/       Core: Vulkan, allocator, hooks, error model, harness, canvas
domain/       Pure logic: document, layer DAG, selection, commands, tool
compositor/   GPU layer compositor (engine + domain bridge)
plugin/       WASM plugin host (stubs)
platform/     Windowing, input, fs, image_io
ui/           Dear ImGui host + widgets (menu_bar, layer_panel,
              outline_panel, page_strip, tool_palette, status_bar,
              debug_overlay, theme)
app/          Executable entry (src/main.cpp) + App class
shaders/      Slang sources (fullscreen, polyline, layer, page_bg)
cmake/        CMake modules (CompilerWarnings, Hardening, NotedModule, Shaders)
docs/architecture/  31 ADRs documenting every cross-cutting decision
tests/        Unit + integration + bench + fuzz scaffolds
```

### What actually works today

✅ Window opens, GPU picked, Vulkan instance + device + swapchain.
✅ Slang shader compiles via `slangc`, SPIR-V loaded at runtime.
✅ Descriptor set + sampler + texture upload + draw call wired.
✅ Frame-in-flight (2) + per-image render_finished semaphores — spec compliant.
✅ Resize handled (OUT_OF_DATE / SUBOPTIMAL → swapchain.recreate).
✅ Input events plumbed: pointer, key, scroll, framebuffer-resize.
✅ Error model: `Result<T> = std::expected<T, Error>`, source location,
   cause chain.
✅ Hook system: typed channels with priorities, RAII subscriptions.
✅ Harness: FeatureFlag, Counter, ScopedTimer, Config, validate.
✅ Profiler: Tracy 0.11 (opt-in via `-DNOTED_ENABLE_TRACY=ON`,
   on-demand), ScopedTimer→zone and Counter→plot. See ADR 0013.
✅ Canvas pipeline: offscreen `CanvasRenderTarget` + composite pass
   over a **6-vertex quad** so camera scale < 1 produces a smaller
   rectangle (not a triangle-shaped cut). See ADR 0014 + fix from
   PR #67.
✅ Stroke engine: vector-ink polyline ribbon, Catmull-Rom centerline,
   pressure-modulated width via `BrushStyle` + `stamp_from_pressure()`.
   Persistently-mapped vertex buffer with geometric grow. Heap-
   allocated, RAII hook subscriptions. See ADR 0015 / 0018 / 0029.
✅ Pen / stylus input: Win32 `WM_POINTER` subclass over GLFW.
   Real pressure + tilt flow through hook events. Synthetic
   mouse-from-pen messages suppressed via `MI_WP_SIGNATURE`. See
   ADR 0017.
✅ LayerGraph domain model: 16 blend modes + 5 layer kinds (wire
   stable), DAG with cycle detection, validate-then-mutate. See ADR 0016.
✅ Layer compositor: walks LayerGraph in topological order, 4
   fixed-function blend modes + counted fallback, single shader,
   per-mode pipelines. See ADR 0019.
✅ Selection domain: rect-list set algebra (add/intersect/subtract),
   canonical normalization, half-open `contains`. See ADR 0020.
✅ Selection GPU mask: `gpu::SelectionMask` (R8_UNORM image with
   layout tracking) + `compositor::SelectionRasterizer`. See ADR 0021.
✅ Compositor masking: `LayerCompositor::composite()` takes an
   optional `SelectionMask*`; layer shader multiplies output by
   mask sample. See ADR 0022.
✅ Document block tree: unified `domain::Document` (group/text/heading/
   code/canvas/image/embed) for notes AND image edits. See ADR 0023.
✅ Command + undo / redo: `Command` abstract base + 7 concrete block
   commands (add/insert/remove/move/set_payload/set_visible/set_name) +
   `UndoStack` (bounded depth, redo invalidation, atomicity). See
   ADR 0024.
✅ Document JSON serialization: `domain::io::document_to_json` /
   `document_from_json` round-trip the block tree. Strict parser,
   version-gated, integer kind ordinals. See ADR 0025.
✅ `.noted` zip container: `platform::io::save_noted_file` /
   `load_noted_file` wrap `document.json` in a standard zip via
   miniz. 256 MB extraction cap. See ADR 0026.
✅ Dear ImGui scaffold: `ui::ImGuiHost` RAII wrapper around
   ImGui + Vulkan + GLFW backends with three-phase frame
   (`begin_frame` / `finalize_frame` / `render_into`). See ADR 0027.
✅ Frame-safe compositor: `LayerCompositor::create()` runs the dummy
   mask's clear + transition synchronously via the new
   `gpu::immediate_submit` helper. composite() is now a pure-draw
   path with per-frame-in-flight descriptor sets. See ADR 0028.
✅ CJK font: ImGuiHost loads an OS-installed CJK TTF/TTC at startup.
   Graceful fallback to ProggyClean on any failure. Override via
   `cfg_.font.cjk_font_path`.
✅ Keyboard shortcuts: Ctrl+N/O/S/Shift+S/Z/Y/Q via
   `ImGui::IsKeyChordPressed`. RouteFocused fix: skip when
   `WantTextInput` to keep Ctrl+Z working inside InputTexts.
✅ Dirty-confirm modal on close / New on dirty doc.
✅ Debug overlay (View → Debug overlay): frame + FPS, CPU plot,
   fallback count, harness counter table.
✅ Theme pass: `noted::ui::theme::apply(ThemeKind)` — dark / light.
✅ App-class architecture: `app/src/main.cpp` is 53 lines doing
   only profile-thread + observers + `App::create+run`. Everything
   else lives in `noted::app::App` (PR #54).
✅ Block rename: F2 in outline panel → inline InputText →
   `SetNameCommand`.
✅ Canvas Camera (Phase A.1): pan + zoom-around-cursor, NaN / inf /
   zero guards, 13 unit tests. See ADR 0029 references.
✅ Vector ink (Phase A.2): Catmull-Rom polyline ribbon, pressure-
   modulated width. See ADR 0029.
✅ Page model (Phase A.3.a): `Page` + `PageList` POD with reflow /
   clamp invariants. 19 unit tests.
✅ Page background rendering (Phase A.3.b): `PageRenderer` GPU
   primitive + `page_bg.slang` (blank / lined / grid / dotted
   patterns with smoothstep AA).
✅ Page strip widget (Phase A.3.c, PR #67): left-rail panel with one
   row per page (label + actual pattern preview + thumbnail rect),
   "+ Add page" footer, right-click → Remove. Click row → camera
   jumps to page (auto-focus on add). `noted::ui::widget::page_strip`
   + `camera_translation_y_for_page` pure helper, 6 unit tests.
✅ AppConfig + relocatable shaders (ADR 0030): typed
   `noted::app::config::AppConfig` (window / canvas / font / assets /
   ui sub-structs), env-var → `noted.config.json` → defaults
   priority, `<exe_dir>/shaders` post-build copy so `build/bin/` is
   zip-distributable.
✅ Build hygiene: zero MSVC warnings on Release. CI matrix verifies
   build + sanitizers + Tracy smoke build on every PR.

✅ **Document linkage (Phase A.3.d, PR #68)**: `domain::Document`
   owns `PageList`. AddPageCommand / RemovePageCommand flow through
   the existing UndoStack. JSON schema v2 with `pages` field;
   reader accepts v1 (empty pages) AND v2.
✅ **Tool state machine (Phase B.1, PR #69, ADR 0031)**:
   `noted::domain::tool::{ToolKind, ToolState, label}` with 6 wire-
   stable ordinals. `noted::ui::widget::tool_palette` side rail.
   `App::tools_` + `tool_settings_for_tool(kind)` returns
   `(BrushStyle, DrawMode)` and swaps both atomically on tool
   switch.
✅ **Canvas pass split + real eraser (Phase B.2, PR #70)**: 4-pass
   pipeline (canvas / strokes target / overlay / swapchain).
   `gpu::StrokeTarget` analogous to `CanvasRenderTarget`.
   `noted::stroke::DrawMode` moved into `stroke_geometry.hpp`;
   `Stroke::mode` snapshotted per-stroke at press time so tool
   toggling never rewrites already-committed strokes. Two
   pipelines in StrokeEngine (draw / erase) with per-slice
   binding. Eraser preserves page pattern through erasure.
✅ **Brush options + colour picker (Phase B.3, PR #72)**:
   `domain::tool::{PenOptions, EraserOptions}` POD payloads on
   `ToolState`. `noted::ui::widget::brush_options` panel with
   size sliders + pressure-curve slider + ColorEdit4. App pushes
   the active tool's settings into the stroke engine every frame
   (live-edit). `brush_from_pen/eraser` clamps NaN/negative
   inputs at the data boundary.
✅ **Rectangle selection tool (Phase B.4, PR #73)**:
   `domain::tool::{SelectionDragMode, rect_from_drag, apply_drag,
   drag_mode_from_modifiers}` pure helpers + `SelectionToolHandler`
   + `selection_overlay` widget. Modifier keys snapshot at press
   time (Shift=add, Alt=subtract, Shift+Alt=intersect, none=
   replace). `StrokeEngine::set_active(bool)` master gate so
   switching tools cleanly commits any in-flight stroke.
✅ **App-layer decomposition R.1 (PR #75, ADR 0032)**:
   `noted::app::input::{ToolInputHandler, ToolInputRouter,
   SelectionToolHandler}` extracted. Future tools (B.5+) ship as
   one ToolInputHandler subclass + one `register_handler` line —
   no growth in `App::install_frame_hook`.
✅ **App-layer decomposition R.2 (PR #76, ADR 0032)**:
   `noted::app::input::CameraController` extracted. Pan + zoom +
   cursor tracking + framebuffer-resize all live in a focused
   80-LOC class with 7 dedicated unit tests. App.cpp: 1402 (B.4)
   → 1319 (-83 LOC, -6%).
✅ **App-layer decomposition R.3 (PR #78, ADR 0032)**:
   `noted::app::frame::RenderPasses` extracted. The 4-pass
   canvas pipeline (canvas / strokes / overlay / swapchain) +
   `render_one_frame` body live in a 360-LOC class with a 16-ref
   `Deps` struct documenting every dependency. App.cpp 1319 →
   1207 (-112). `recreate_swapchain` stays in App (owner work).
✅ **App-layer decomposition R.4 (PR #79, ADR 0032)** — **FINAL**:
   `noted::app::ui::UiPanels` extracted. The entire `draw_widgets`
   body (panels + command dispatches + per-frame state syncs) lives
   in a 429-LOC class with a 20-ref Deps struct. App's
   `draw_widgets` is now a one-line forward. App.cpp 1207 →
   **1002** (-205). Cumulative R.1-R.4: **1402 → 1002, -400 LOC,
   -28.5%**.
✅ **B.7.b.1 — image asset registry (PR #98)**: `ImageAssetRegistry`
   maps `AssetId` → `ImTextureID`, observes `Document::images()` and
   reclaims handles on remove. `ImagePrimitive` gains `AssetId`.
   `.noted` schema v6.
✅ **B.7.b.2 — file picker + decode (PR #100)**: "Pick image…"
   button wired via `nativefiledialog-extended`, stb_image decode
   to capture intrinsic dimensions, fresh `AssetId` allocated and
   stamped onto `ImageOptions::pending_asset_id`. (Real GPU upload
   lives in the still-open B.7.b.2b.)
✅ **Page-on-desk visual model + strokes persistence (PR #101,
   ADR 0033)**: single neutral desk fills viewport outside pages;
   each page carries soft drop shadow + AA edges; canvas clear
   matches desk so no margin band appears under zoom-out. Strokes
   graduate to `Document::strokes()` with Add/RemoveStrokeCommand
   (`.noted` v7). Auto shape detection during stroke commit.
✅ **Stroke storage refactor (PR #102)**: StrokeEngine drops its
   internal vector — host (Document) owns the strokes vector and
   engine reads it by reference each frame. Eliminates the
   double-source-of-truth that was masking off-by-one bugs across
   undo/redo.
✅ **Smart-shape recognizer (PR #103)**: freehand strokes auto-snap
   into `ShapePrimitive` (rect / ellipse / line) when geometric
   tolerance + dwell heuristic pass. Hold-to-keep-stroke pen
   gesture honoured.
✅ **Qt 6 UI migration (ADR 0034, PRs #104-#108)** — the v1.0
   product UI replacement for Dear ImGui shell:
   - **Phase 0 (PR #105)**: Qt 6.7+ added as required dep,
     `noted_app` links it but no Qt API used yet.
   - **Phase 1 (PR #106)**: `platform::Window` swapped from GLFW
     to `QWindow` + `QVulkanInstance`. Event pump runs through Qt.
   - **Phase 2 (PR #107)**: Mac-style window chrome — frameless
     window, traffic-light controls, vibrant titlebar blur.
   - **Phase 3 (PR #108)**: 12-o'clock floating toolbar built in
     QML, anchored to the top centre. Dear ImGui still owns the
     side panels until Phase 4 ports them.
✅ **Shape clipboard (PR #109)**: Ctrl+X / Ctrl+C / Ctrl+V / Delete
   chords operate on the currently-selected shape primitives via
   `Selection` + a `domain::ClipboardBuffer`.
✅ **Floating colour picker panel (PR #110)**: right-side
   OpenCanvas-style swatches + HSV wheel + history strip.
✅ **Lasso tool (PRs #111-#113)**:
   - **Step 1 (PR #111)**: `domain::Selection::Polygon` variant.
   - **Step 2 (PR #112)**: `app::input::LassoToolHandler` — drag
     deposits polygon vertices, release closes the loop and
     mutates `Selection`.
   - **Step 3 (PR #113)**: `compositor::SelectionRasterizer`
     scanline-fills the polygon into the `SelectionMask`.
✅ **Navigator panel (PR #114)**: scaled-down canvas thumbnail with
   live viewport rectangle. Click-drag the rect to pan camera.
✅ **Layer painting + Photoshop-grade workspace (PR #115)**:
   strokes target a chosen `LayerId`. Workspace docks rearranged
   to put layers/colour on the right rail.
✅ **AAA brush library (PR #116)**: shipped presets (pencil / ink
   pen / felt-tip / soft brush / charcoal / highlighter), custom
   brush save/load, persisted to a separate `~/.noted/brushes.json`.
   Picker widget with thumbnail strokes.
✅ **brush options single-scope + camera scroll gate (PR #117)**:
   brush_options panel reads one ToolState (no per-tool dup); the
   camera scroll handler now gates on `WantCaptureMouse` so
   scrolling inside ImGui panels no longer zooms the canvas.
✅ **Real soft-edge SDF brush (PR #118)**: `softness_ratio` now
   drives a real SDF-edge alpha falloff in `polyline.slang`. The
   "no fake sliders" gate from the previous handoff lifted —
   softness now actually does something. Cards rephrased
   honestly (no "approximation" wording).
✅ **Pressure curve editor (PR #119)**: 2-handle bezier
   `PressureCurve` replaces the single `alpha_gamma` scalar.
   `brush_options` gains a visual editor — drag the two handles
   on a 256×128 widget to author the pressure → alpha mapping.
✅ **Velocity-aware brush size (PR #120)**: per-sample velocity
   tapers width via `velocity_blend ∈ [0,1]`. Tessellator reads
   `t` (seconds-since-stroke-start) from each `StrokeSample`.
✅ **Tilt-aware calligraphy width (PR #121)**: pen tilt
   (`tilt_x`, `tilt_y` unit vector) modulates ribbon width along
   tilt direction via `tilt_blend ∈ [0,1]`. Tilt-pen feel.
✅ **`.noted` v9 — pen-dynamics in save/load (PR #122, ADR 0035)**:
   sample stride 3 → 6 to persist `t`, `tilt_x`, `tilt_y`. Style
   block gains `pc` (PressureCurve handles), `vb`, `tb`. v1..v8
   readers retained for forward-compat.
✅ **Layer mutation commands + slider coalescing (PR #123)**: every
   layer panel action (add/remove/move/set_visible/set_opacity/
   set_blend_mode) now flows through a Command on `UndoStack`.
   Sliders coalesce continuous drags into a single command on
   release (so dragging opacity doesn't fill the undo history).
✅ **Layer-UI keyboard shortcuts + active-layer status (PR #127)**:
   Ctrl+Shift+N (new layer), Ctrl+J (duplicate), Delete (remove),
   Alt+]/[ (cycle active). Status bar shows the active layer name.
✅ **Layer merge ops (PR #128)**: Merge Down (Ctrl+E), Merge
   Visible (Ctrl+Shift+E), Flatten Image — all GPU-side via the
   existing compositor, results read back into a fresh
   `RasterLayer`. Commands so undo works.

### What does NOT work yet (by design — not bugs)

- **12 of 16 layer blend modes** — `LayerCompositor` ships
  fixed-function pipelines for normal / multiply / linear_dodge /
  screen and counts the rest as a fallback. Phase C item.
- **Asset / history embedding** in the `.noted` archive — image
  blobs decoded by `stb_image` live only in RAM; save/load
  round-trips an `AssetId` to nothing. **B.7.b.3** is the
  remaining slice.
- **ADR 0034 Phase 4** — the side panels (`tool_palette`,
  `page_strip`, `layer_panel`, `brush_options`, `navigator`,
  `status_bar`, `outline_panel`, `debug_overlay`) still render
  through Dear ImGui inside the QWindow. Phase 4 ports them to
  QML one widget at a time.
- **PDF export / page templates / pen-button mapping** — Phase D
  Goodnotes-polish backlog still open.
- **Pen pressure on macOS / Linux** — Win32 WM_POINTER only today
  (the QWindow shell preserves the same pen-event path via a
  native event filter on Windows; macOS / Linux pen ports still
  pending).
- **Edit coalescing** in `UndoStack` for non-slider commands
  (every keystroke is still one undo entry; layer-slider
  coalescing landed in PR #123 but text edits did not).
- **GPU tests in CI** (CI runners have no GPU).

---

## How to pick up work

### Prerequisites (one-time, ~15 min)

PowerShell as Admin:
```powershell
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id KhronosGroup.VulkanSDK -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
winget install --id GitHub.cli -e
winget install --id astral-sh.uv -e   # for clang-format-18
uv tool install 'clang-format==18.1.8'
git lfs install
```
Restart the shell.

**Qt 6.7+ is now required** (ADR 0034 Phase 0, PR #105). Install
via the Qt online installer or `aqt`, then set `CMAKE_PREFIX_PATH`
to the kit's `lib/cmake/Qt6` dir (e.g.
`C:/Qt/6.8.1/msvc2022_64`). See `docs/SETUP.md` for the exact
component list (Qt Quick, Qt Quick Controls 2, Qt Shader Tools).

The build matrix expects `clang-format-18` on PATH. Without it the
lint CI job still runs (it installs its own), but local pre-commit
runs require it. Verify with `clang-format --version` → `18.1.8`.

### Clone + build + run

```powershell
git clone https://github.com/yoo9857/noted.git
cd noted
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
.\build\bin\noted_app.exe
```

Expected on first launch: a 1600×1000 window with **3 demo pages**
(grid / lined / dotted backgrounds) stacked vertically on the canvas,
the Tool palette + Page strip + Layer panel + Outline panel docked,
and pen / mouse drag deposits vector-ink strokes that survive across
frames. Scroll wheel zooms around the cursor; middle-drag pans.

### Read first (in order — ~30 min)

1. **This file** — the "Where we are" + "Roadmap" sections above are
   the entry point. Skim "What does NOT work yet" to know what's
   intentionally absent vs broken.
2. [`docs/architecture/README.md`](docs/architecture/README.md) → the
   31 ADRs in numeric order. **Read all of them** before changing
   cross-cutting code. ADR 0001 (C++23 + Vulkan), 0003 (Result<T>),
   0004 (harness), 0011 (2026 baseline), 0016 (LayerGraph), 0019
   (compositor), 0023 (Document), 0024 (Command/Undo), 0027 (UI
   stack), 0029 (vector ink), 0030 (AppConfig), 0031 (tool state)
   are the most-referenced; the rest fill in details.
3. [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch protocol, commit
   convention, code style.
4. [`README.md`](README.md) — project overview.

### Goodnotes + Photoshop unified canvas — phased plan

Engine MVP + UI shell are ready. The product vision is a single
document that delivers **Goodnotes UX** (stylus-first, page navigation,
infinite canvas feel) on **Photoshop depth** (layers, blend modes,
filters, color management). Phased to keep each PR focused:

| Phase | Item | Status |
|---|---|---|
| A.1 | Camera pan + zoom (PR #57) | ✅ |
| A.2 | Vector ink — Catmull-Rom polyline ribbon, pressure-modulated width (PRs #59-61, ADR 0029) | ✅ |
| A.3.a | Page model — `Page` + `PageList` POD + 19 unit tests (PR #63) | ✅ |
| A.3.b | Page background rendering — `PageRenderer` + `page_bg.slang` patterns (PR #64) | ✅ |
| A.3.c | Page strip panel + Add/Remove page UI + camera focus (PR #67) | ✅ |
| A.3.d | Document linkage — `PageList` ownership moves into Document, mutation via Command, persists to `.noted` v2 (PR #68) | ✅ |
| B.1 | Tool state machine — `ToolKind` / `ToolState` + tool palette widget + pen/eraser brush swap (PR #69, ADR 0031) | ✅ |
| B.2 | Real eraser via canvas pass split — `gpu::StrokeTarget` + destination-out blend, per-stroke mode snapshot, page pattern survives erasure (PR #70) | ✅ |
| B.3 | Per-tool option payloads — `PenOptions` / `EraserOptions` + `brush_options` widget + colour picker + size sliders (PR #72) | ✅ |
| B.4 | Rectangle selection tool — `SelectionToolHandler` + `selection_overlay` + modifier-key set ops (PR #73) | ✅ |
| B.5 | Shape tool — rectangle + ellipse, `ShapeToolHandler` + `shape_overlay`, app.cpp Δ +8 LOC (PR #81) | ✅ |
| B.6 | Text tool — click+type, `TextToolHandler` + `text_overlay` (pure-domain API), app.cpp Δ +5 LOC (PR #82) | ✅ |
| B.7 | Image tool — click-to-place placeholder, `ImageToolHandler` + `image_overlay`, app.cpp Δ +7 LOC (PR #84) | ✅ |
| **P.S.1** | **Persistence consolidation — shapes graduate** to `Document::shapes()` + `Add/RemoveShapeCommand` + `.noted` v3 (PR #86) | ✅ |
| **P.S.2** | **Persistence consolidation — texts graduate** to `Document::texts()` + `Add/RemoveTextCommand` + `.noted` v4 (PR #87) | ✅ |
| **P.S.3** | **Persistence consolidation — images graduate** to `Document::images()` + `Add/RemoveImageCommand` + `.noted` v5 (PR #88) | ✅ |
| **B.7.b.1** | **ImageAssetRegistry + AssetId on ImagePrimitive + `.noted` v6 (PR #98)** | ✅ |
| **B.7.b.2** | **"Pick image…" + stb_image decode + AssetId allocation (PR #100)** | ✅ |
| B.7.b.3 | Asset bundle in `.noted` zip — encoded payload alongside `document.json` | |
| B.7.b.2b | VMA `VkImage` GPU upload + `ImTextureID` registry — re-decode from bundled bytes | |
| **D.1** | **Page-on-desk visual model (ADR 0033, PR #101)** — desk fill, drop shadows, AA page edges, strokes graduated to `Document` (`.noted` v7) | ✅ |
| **D.2** | **Stroke storage refactor (PR #102)** — engine reads Document, no internal vector | ✅ |
| **D.3** | **Smart-shape recognizer (PR #103)** — freehand → ShapePrimitive snap with dwell heuristic | ✅ |
| **U.0** | **Qt 6 build integration (ADR 0034 Phase 0, PR #105)** | ✅ |
| **U.1** | **GLFW → QWindow shell (ADR 0034 Phase 1, PR #106)** | ✅ |
| **U.2** | **Mac-style window chrome (ADR 0034 Phase 2, PR #107)** | ✅ |
| **U.3** | **12-o'clock floating toolbar in QML (ADR 0034 Phase 3, PR #108)** | ✅ |
| U.4 | ADR 0034 Phase 4 — port the rest of the ImGui side panels into QML | |
| **D.4** | **Shape clipboard — Cut / Copy / Paste / Delete (PR #109)** | ✅ |
| **D.5** | **Floating colour picker panel (PR #110)** | ✅ |
| **D.6** | **Lasso tool — polygon Selection + scanline rasteriser (PRs #111-#113)** | ✅ |
| **D.7** | **Navigator panel — thumbnail + viewport rect (PR #114)** | ✅ |
| **C.1** | **Layer painting + Photoshop-grade workspace (PR #115)** — strokes target a `LayerId` | ✅ |
| **C.2** | **AAA brush library — presets + custom + persistence (PR #116)** | ✅ |
| **C.3** | **Real soft-edge SDF brush + honest cards (PR #118)** — `softness_ratio` finally does something | ✅ |
| **C.4** | **Pressure curve editor — 2-handle bezier `PressureCurve` (PR #119)** | ✅ |
| **C.5** | **Velocity-aware brush size — speed taper (PR #120)** | ✅ |
| **C.6** | **Tilt-aware calligraphy width (PR #121)** | ✅ |
| **C.7** | **`.noted` v9 — pen-dynamics in save/load (PR #122, ADR 0035)** | ✅ |
| **C.8** | **Every layer mutation is a Command + slider coalescing (PR #123)** | ✅ |
| **C.9** | **Layer-UI keyboard shortcuts + active-layer indicator (PR #127)** | ✅ |
| **C.10** | **Layer merge down / merge visible / flatten image (PR #128)** | ✅ |
| **R.1** | **App-layer decomposition** — `ToolInputRouter` + `SelectionToolHandler` extracted (PR #75, ADR 0032) | ✅ |
| **R.2** | **App-layer decomposition** — `CameraController` extracted (PR #76, ADR 0032) | ✅ |
| **R.3** | **App-layer decomposition** — `RenderPasses` (4-pass canvas pipeline) extracted (PR #78, ADR 0032) | ✅ |
| **R.4** | **App-layer decomposition** — `UiPanels` (draw_widgets body) extracted (PR #79, ADR 0032) | ✅ |
| C   | Photoshop depth — layer panel ops, shader blend modes (12 missing), filter pipeline, color management | |
| D   | Goodnotes polish — smart shapes, lasso + transform handles, pen-button mapping, page templates, PDF export | |
| E   | (optional) Native chrome — ImGui → Qt/Slint per ADR 0027 v1.0 boundary | |

**Deferred until UI validation:**
- P5 #13 `feat/shader-objects` (`VK_EXT_shader_object`) and
  the rest of P5 modern Vulkan extensions
- Asset / LayerGraph / history embedding in the `.noted` archive
- Edit coalescing in `UndoStack`
- macOS / Linux pen-input ports

### Next session — pick up here

The last 28 PRs (#100 → #128) landed three major lines of work:
**Qt 6 migration** through Phase 3 (ADR 0034); **pen-dynamics
stack** culminating in `.noted` v9 (ADR 0035); **layer ops**
through merge-down / flatten-image with every mutation on the
undo stack. The pickable next bites:

**Option 1 — B.7.b.3 asset bundle in `.noted` zip (recommended
finish-the-job).** B.7.b.1 (registry + AssetId, PR #98) and
B.7.b.2 (picker + decode, PR #100) shipped, but the decoded
pixel bytes get discarded immediately and `.noted` save/load
round-trips an `AssetId` to nothing on the other side. Land the
third slice:

  - `ImageAsset` gains an encoded-payload field (the original
    PNG/JPG bytes, not decoded RGBA — much smaller and preserves
    quality across re-saves).
  - `App::run_image_picker` reads the file bytes alongside the
    decode it already does.
  - `platform::io::save_noted_file` writes each registered
    asset's payload as its own zip member (`assets/<id>`);
    `load_noted_file` extracts them back into `source_bytes`.
  - Schema bump to v10 (zip-layout-only — the JSON shape stays
    constant); v1..v9 readers retained for forward-compat.
  - 256 MB document.json cap from ADR 0026 stays; add a
    per-asset extraction cap (64 MiB) for the new path.

  Branch: `feat/noted-asset-bundle`.

**Option 2 — ADR 0034 Phase 4: port remaining ImGui panels to
QML.** Phases 0-3 landed the Qt build, QWindow shell, Mac chrome,
and 12-o'clock toolbar (PRs #105-#108). The side rails
(`tool_palette`, `page_strip`, `layer_panel`, `brush_options`,
`navigator`, etc.) are still Dear ImGui. Phase 4 lifts them into
QML one panel at a time; the canvas itself stays in the Vulkan
viewport that QWindow already hosts.

  Likely sub-PRs: one widget at a time, smallest first
  (`status_bar`, then `tool_palette`, then `layer_panel`,
  `brush_options` last because it has the most controls).
  Branch family: `feat/qml-panel-*`.

**Option 3 — Phase C: shader blend modes (still open).** 12 of
the 16 declared blend modes in `LayerGraph` fall through to the
counted fallback in `LayerCompositor`. Pure GPU work — write
the math in `shaders/layer.slang`, add the pipelines, drop the
fallback counter. No domain churn, no UI churn.
Branch: `feat/layer-blend-modes`.

**Option 4 — Phase D Goodnotes polish — remaining items.** Lasso
(PRs #111-#113) and smart shapes (PR #103) done. Open: pen-button
mapping, page templates, PDF export.

**Recommendation: Option 1** — finishes a half-implemented user-
visible promise (images persist) and is scoped to one PR.

---

### Earlier proposed B.7 plan (kept for reference)

Branch name: `feat/image-tool`.

Shape of the work — same pattern as B.5 / B.6:

1. **`domain/tool/image_input`** — pure data: `ImageOptions`
   (placeholder for tint / opacity), `ImagePrimitive` (anchor +
   width/height + decoded RGBA buffer or asset id + opacity).
   Anchor is top-left in canvas pixels, matching `TextPrimitive`'s
   convention.
2. **`app/src/input/ImageToolHandler`** — interaction model is
   **click-to-place** (or **paste from clipboard / drop from OS**
   long-term; v0.x can start with a stub "click places a fixed
   placeholder texture" and iterate). Snapshots `ImageOptions` at
   PRESS like every other tool.
3. **`ui/widget/image_overlay`** — pure-domain API (refs to
   `vector<ImagePrimitive>` + canvas-to-screen). Renders via
   `ImGui::GetBackgroundDrawList()->AddImage`. The ImGui image API
   needs an `ImTextureID` — for v0.x we can stage via a small
   GPU-side texture registry (one Vulkan image per primitive,
   freed on remove) or punt and use `AddRectFilled` placeholders
   until the texture path lands. **Decide on PR open**.
4. **`brush_options` Image section** — minimal v0.x: a "Pick
   image…" button (nativefiledialog-extended already available).
5. **`menu_bar` Image overlay toggle** — same pattern as Text.
6. **App's `install_frame_hook` gains ONE line.**
7. **Tests** — pure-logic helpers (clamping, anchor maths) +
   `ImageOptions` equality + defaults.
8. **Smoke** — pick Image, click → placeholder shows on canvas.

**App.cpp budget: ≤ +8 LOC** (same as B.5). Third tool in a
row confirms the pattern.

After B.7: **persistence** for all B-series primitives — shapes /
texts / images currently live in App-owned vectors. They should
graduate to `Document::shapes() / texts() / images()` with
`Add*Command` + `Remove*Command` so undo / save / load round-trip
through the `.noted` archive. That's a single PR that touches
all three vectors at once (analogous to A.3.d which graduated
PageList into Document).

The pure-data + pure-handler split that B.6 enforced (lifting
`EditingState` into `domain::tool::TextEditingState` so `ui` has
no `app/` dependency) is the template. New tools should follow
the same boundaries: domain owns the data + reducers, app owns
the handler + event glue, ui owns the rendering + interaction
widgets — never reaching across.

This is the third slice of the App-layer decomposition. **Zero
behaviour change** is the contract — 340/340 tests continue to pass
at every intermediate state.

Concrete plan:

1. **New `app/src/frame/render_passes.{hpp,cpp}`** —
   `noted::app::frame::RenderPasses` class. Holds **references**
   to the GPU resources App owns (not ownership): canvas,
   strokes_target, page_renderer, layer_compositor, scene,
   stroke_engine, composite pipeline + overlay pipeline +
   pipeline layout + descriptor sets, imgui_host, camera.
   Constructor takes a `Deps` struct so the ref list is
   self-documenting.
2. **Move `record_canvas_pass` / `record_strokes_pass` /
   `record_overlay_pass` / `record_swapchain_pass`** into
   RenderPasses as private member functions. Each becomes
   `void record_*(VkCommandBuffer, VkExtent2D) const` reading
   from the captured references.
3. **`RenderPasses::render_frame(Renderer&, Swapchain&, ...)`** —
   the per-frame orchestrator. Today's `App::render_one_frame`
   body (build clear values + pass descriptors + call
   `renderer.render_with_canvas`) moves here. Returns the same
   `Result<void>` with the OUT_OF_DATE / SUBOPTIMAL recoverable
   codes.
4. **`recreate_swapchain` stays in App** — it touches the
   swapchain itself + the renderer's per-image semaphores +
   reallocates canvas + strokes_target. That's owner work, not
   pass work. The descriptor re-binding after resize moves into
   a small helper `App::rebind_composite_descriptors_()` to
   document the contract.
5. **App becomes a thin orchestrator**: `App::render_one_frame`
   becomes one line — `return render_passes_->render_frame(...)`.
   The 4 record_* methods on App disappear entirely.
6. **Tests** — RenderPasses is GPU-dependent so unit tests
   can't exercise the recording itself. What's testable is the
   `Deps` struct's validation (a future paranoia helper) and
   the smoke run remains the gating criterion.
7. **Smoke** — pen still draws, eraser still erases preserving
   page pattern, selection rect still appears, fb resize still
   keeps the canvas aligned. Stderr stays at 0 bytes.

After R.3: **R.4 — `UiPanels`** extraction (draw_widgets +
handle_menu_actions + per-frame UI state pushes).

After R.4: **Phase B.5 — Shape tool** ships as the FIRST tool
under the fully-decomposed pattern: one `ShapeToolHandler`
subclass + one `register_handler` line in App's startup. App
itself shouldn't grow at all for B.5.

---

## Known debt and gotchas

### Format drift — resolved
clang-format-18 is mandatory on every PR. Local setup:
`uv tool install clang-format==18.1.8` (or `pip install clang-format==18.1.8`).
The lint CI job blocks merges on drift; run
`clang-format-18 -i path/to/file.cpp` to fix.

### Conventional-commit scope characters
The `commit message lint` CI job enforces
`^(feat|fix|chore|...)(\([a-z0-9._-]+\))?!?: .+`. Scopes can only
contain `[a-z0-9._-]` — no `+`, no spaces, no uppercase. When a
change touches multiple modules, pick the dominant one or use a
hyphenated multi-scope (`canvas-ui`, `domain-app`). Don't use `+`.

### Force-pushing PR branches
If you need to amend a commit (e.g. to fix the conventional-commit
subject), `git push --force-with-lease` is OK on personal feature
branches but **never** on `main`. Branch protection should block
that anyway; if it doesn't, treat it as a configuration bug.

### Stacked PR rebase pattern
When a PR is built on top of an un-merged predecessor and the
predecessor merges via squash, the rebase will fault on the
predecessor's commits (which are now squashed into a single commit
on main). Use `git rebase --skip` for each conflicted commit — git
also auto-drops commits it detects as "already upstream" once you
move past the first.

### `scripts/build_and_run.cmd` was a local helper
Hardcoded paths for a specific machine. **Not committed** — recreate per
machine or run the cmake commands directly.

### Vulkan SDK version drift
ADR 0011 pins SDK ≥ 1.4.309. CI installs 1.4.309.0. The local dev guide
in `docs/SETUP.md` says the same. If you bump it, bump all three places
(CI env, SETUP.md, ADR 0011).

### Slang SPIR-V entry name
**Slang renames every entry to `"main"` in SPIR-V.** Use
`add_stage(stage, module, "main")` (the builder default). Don't pass the
Slang function name — see ADR 0012 (corrected) for the painful learning.

### Hardcoded shader path — resolved
`AppConfig::assets.shader_dir` + `resolve_shader_dir(cfg)` walk:
config override → `NOTED_SHADER_DIR` env → `<exe_dir>/shaders` →
compile-time fallback. A CMake post-build step copies `.spv` files
next to the `.exe` so `build/bin/` is zip-distributable. See ADR 0030.

### Fullscreen-triangle vs camera scale
The composite pass uses a **6-vertex quad** rather than the classic
fullscreen-triangle. The fullscreen-triangle pattern relies on the
GPU clipping over-spilling vertices against the [-1, 1] NDC box;
that breaks under camera scale < 1 because the triangle shrinks
inside the visible region. A quad has no over-spill so it shrinks
to a rectangle, which is the correct "zoomed out" behaviour. See
`shaders/fullscreen.slang`'s vs_main + the comment in
`App::record_swapchain_pass`. Fixed in PR #67.

### Semaphore semantics
We use binary semaphores with per-image render_finished. When we
migrate to timeline semaphores (`feat/timeline-semaphore-renderer`),
the per-image structure changes. Read ADR 0008 before touching
`engine/src/gpu/renderer.cpp`.

---

## Useful commands

```powershell
# Sourcing MSVC env (each shell session)
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

# Clean rebuild
rm -r build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run unit tests
.\build\tests\unit\noted_unit_tests.exe --gtest_brief=1

# Open PR via gh CLI
gh pr create --base main --head <branch> --title "..." --body "..."

# Watch CI on a PR
gh pr checks <number> --watch

# Squash-merge a PR (after CI green + review)
gh pr merge <number> --squash --delete-branch --subject "..." --body "..."

# Run with Vulkan validation explicit (already on by default)
$env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
.\build\bin\noted_app.exe

# Disable validation for perf testing
$env:VK_INSTANCE_LAYERS = ""
.\build\bin\noted_app.exe
```

---

## When in doubt

- **Architecture question?** Read the relevant ADR in
  `docs/architecture/`. If your decision contradicts an ADR, write a new
  ADR that supersedes it.
- **Build broken on Linux but Windows works (or vice versa)?** Check the
  CI matrix log — `sanitizers` job catches most cross-platform issues.
- **Vulkan validation noise?** Treat every validation warning as a real
  bug. Suppressing validation is forbidden in this codebase.
- **Tempted to add a feature flag for "old vs new"?** Don't. Replace the
  old path entirely. See ADR 0012 (Slang fully replaced GLSL — no half
  migrations).
- **Tempted to amend / force-push to fix a CI failure?** Prefer adding
  a new commit on top. Amend only for **the very latest commit** when
  no one else has pulled the branch (which, in this solo-dev repo, is
  almost always true). Force-pushing the squashed PR branch is the
  natural follow-up. See the "Force-pushing PR branches" gotcha above.

The foundation is solid. Phase B is in flight.
