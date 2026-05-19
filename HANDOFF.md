# Handoff — where the project is and what's next

**Last updated:** 2026-05-19 · **main HEAD:** `65e2d4e` (clean, 0 open PRs)

Goal: a professional note-taking + raster image editor that exceeds
Goodnotes (vector ink, stylus-first) AND Photoshop (raster layers,
filters) in one unified document. AAA-grade performance, maintainability,
exception handling.

---

## Where we are

**The app builds, runs, and is interactive end-to-end** (within v0.x
demo scope). A 1600×1000 window opens, the GPU is picked, a
`LayerCompositor` walks a 4-layer demo `LayerGraph` (normal / multiply
/ linear_dodge blend modes), the stroke engine overlays pen-input ink
on top, and a Dear ImGui-driven product shell renders on top with:

  - **Menu bar** (File / Edit / View / About). File's New / Open /
    Save / Save As back the `.noted` JSON+zip format via a native
    OS dialog (nativefiledialog-extended). Edit's Undo / Redo back
    the live `UndoStack`; Edit → Add Block emits `AddBlockCommand`
    for any of the 7 BlockKinds. The title bar shows the filename
    + `*` dirty marker. Keyboard shortcuts (Ctrl+N/O/S/Shift+S/Z/Y/Q)
    fire the same signals as the menu items. A modal asks
    Save / Discard / Cancel when the user closes the window or
    starts a New on a dirty document.
  - **Debug overlay** (View → Debug overlay; off by default) — small
    floating window with frame index + FPS, a 120-sample CPU-time
    line plot, the LayerCompositor fallback count, and every
    `harness::Counter` row.
  - **Layer panel** — visibility checkbox per layer wires through
    `LayerGraph::set_visible`; compositor reflects next frame.
  - **Outline panel** — tree view of `Document.preorder` with
    click-to-select.
  - **Status bar** — frame index + FPS pinned to bottom.

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
- **GLFW 3.4** for windowing + input
- **stb_image** for PNG/JPG decode
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

### Repo layout

```
engine/       Core: Vulkan, allocator, hooks, error model, harness
domain/       Pure logic: document, layer DAG, selection, commands, CRDT
compositor/   GPU layer compositor (engine + domain bridge)
plugin/       WASM plugin host (stubs)
platform/     Windowing, input, fs, image_io
ui/           View layer (stubs — UI tech TBD)
app/          Executable entry (src/main.cpp)
shaders/      Slang sources (fullscreen, stamp, layer)
cmake/        CMake modules (CompilerWarnings, Hardening, NotedModule, Shaders)
docs/architecture/  27 ADRs documenting every cross-cutting decision
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
✅ Canvas pipeline: offscreen `CanvasRenderTarget` + two-pass renderer
   (canvas → composite). Foundation for strokes/layers. See ADR 0014.
✅ Stroke engine (MVP): mouse drag draws anti-aliased SDF-disk stamps
   into the canvas via a push-constant pipeline. Heap-allocated, RAII
   hook subscriptions. See ADR 0015. Pressure-driven brush via
   `BrushStyle` + `stamp_from_pressure()` curve. See ADR 0018.
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
   canonical normalization, half-open `contains`. Domain-only;
   GPU rasterization PR is next. See ADR 0020.
✅ Selection GPU mask: `gpu::SelectionMask` (R8_UNORM image with
   layout tracking) + `compositor::SelectionRasterizer`
   (CPU rasterize → buffer-to-image copy). Pure rasterize step
   unit-tested without a GPU. See ADR 0021.
✅ Compositor masking: `LayerCompositor::composite()` takes an
   optional `SelectionMask*`; layer shader multiplies output by
   mask sample. Internal 1×1 "all selected" dummy keeps shader
   unconditional when caller passes nullptr. Lazy dummy init on
   first composite() call. See ADR 0022.
✅ Document block tree: unified `domain::Document` (group/text/heading/
   code/canvas/image/embed) for notes AND image edits. Tree with
   ordered children, parent pointers for O(1) up; payload variant
   with side-store IDs for heavy data. 30 unit tests. See ADR 0023.
✅ Command + undo / redo: `Command` abstract base + 7 concrete
   commands (add/insert/remove/move/set_payload/set_visible/
   set_name) over `Document`. Inverse-based (not snapshot) so the
   stack stays small. `UndoStack` with bounded depth, redo
   invalidation, atomicity. `Document::restore_subtree` as the
   precise inverse of `remove_block`. 27 unit tests. See ADR 0024.
✅ Document JSON serialization (v1 of `.noted`): `domain::io::
   document_to_json` / `document_from_json` round-trip the block
   tree. Schema is strict (unknown keys rejected), version-gated
   (`"version": 1`), uses wire-stable integer kind ordinals
   (ADR 0023). 26 unit tests. See ADR 0025.
✅ `.noted` zip container: `platform::io::save_noted_file` /
   `load_noted_file` (+ `document_to_archive_bytes` /
   `document_from_archive_bytes` for in-memory use) wrap
   `document.json` in a standard zip via miniz. `assets/` +
   `graphs/` subdirs reserved for future asset + LayerGraph
   stores. 256 MB extraction cap. 13 unit tests. See ADR 0026.
✅ Dear ImGui scaffold: `ui::ImGuiHost` RAII wrapper around
   ImGui + Vulkan + GLFW backends with three-phase frame
   (`begin_frame` / `finalize_frame` / `render_into`). The
   split avoids a dangling ImGui frame when the renderer
   bails on a swapchain-out-of-date. 6 unit tests cover
   create() rejection paths. See ADR 0027.
✅ Compositor wire-up: `app/main.cpp` now drives the canvas
   pass via `compositor::LayerCompositor::composite()` walking
   a demo `domain::LayerGraph` with 4 layers (normal / multiply
   / linear_dodge / normal) — exercises every FF blend mode the
   compositor implements. Stroke ink still overlays the layer
   composite; ImGui still overlays the swapchain composite.
   Textured-quad demo + checkerboard / sample.png loading is
   gone.
✅ IM_ASSERT routing: `ui/imgui_user_config.hpp` overrides
   `IM_ASSERT` so ImGui invariant violations route through
   `noted::harness::validate` (ErrorObserved hook channel)
   AND still hit `assert()` for fail-fast in debug. Closes
   ADR 0027's deferred routing promise.
✅ Product shell (first cut): menu bar (File / Edit / View /
   About), layer panel observing + mutating the scene graph
   (visibility toggle wires through `set_visible()`), status
   bar pinned to the bottom of the viewport (frame index +
   FPS). `ImGui::ShowDemoWindow` retired to View → ImGui Demo
   toggle, off by default.
✅ Document + UndoStack wired: empty `domain::Document` lives
   in main.cpp. Outline panel (tree view of
   `Document.preorder()` with selection state) renders the
   live model. Edit menu's Undo / Redo back the live
   `UndoStack`; Edit → Add Block submenu emits
   `AddBlockCommand` under the selected block (group),
   document root, or invalid_block_id (first block becomes
   the root). Selection follows the newly-added block.
✅ File menu wired: `platform::io::pick_noted_open` /
   `pick_noted_save` wrap nativefiledialog-extended (zlib, IFileDialog
   on Win32). main.cpp tracks `current_path` + `saved_undo_size`;
   Save falls through to Save As when the document has no on-disk
   backing path. Title bar shows `noted — <filename> [*]`. Save As
   force-appends `.noted` so the file always round-trips through the
   same filter. See P4 #18d-file.
✅ Frame-safe compositor: `LayerCompositor::create()` runs the dummy
   mask's clear + transition synchronously via the new
   `gpu::immediate_submit` helper (transient pool + one-time-submit CB
   + fence wait). composite() is now a pure-draw path with per-frame-
   in-flight descriptor sets and view-cached descriptor writes — zero
   barriers, zero spec violations. See ADR 0028.
✅ CJK font: ImGuiHost loads an OS-installed CJK TTF/TTC at startup
   with `GetGlyphRangesKorean()` + 2048×2048 atlas. main.cpp probes
   malgun.ttf / AppleSDGothicNeo / Noto Sans CJK KR / Nanum Gothic in
   that order. Graceful fallback to ProggyClean on any failure — never
   blocks `ImGuiHost::create()`.
✅ Keyboard shortcuts: Ctrl+N/O/S/Shift+S/Z/Y/Q wired via
   `ImGui::IsKeyChordPressed` (RouteGlobal default). Save fall-through
   matches the menu; Undo / Redo gated by `can_undo` / `can_redo` so
   an empty stack doesn't print error noise. See P4 #18e.
✅ Dirty-confirm modal: closing the window (X / File → Quit /
   Ctrl+Q) or starting a New on a dirty document opens a Save /
   Discard / Cancel modal. One state machine, single arming flag,
   double-X-click race guarded. See P4 #18f.
✅ Debug overlay: View → Debug overlay (off by default). Frame
   index + FPS, 120-sample CPU-time line plot driven by
   `on_frame_end`, LayerCompositor fallback count, and a
   name/value table of every registered `harness::Counter`. See
   P4 #18g.
✅ Build hygiene: zero MSVC warnings on Release. Third-party headers
   (GLFW/VMA/stb/Tracy/GoogleTest) marked SYSTEM via FetchContent so
   their warnings can't leak. `/Ob[0-9]` collisions removed at the
   cache layer.
✅ CI matrix verifies build + sanitizers + Tracy smoke build on every PR.

### What does NOT work yet (by design — not bugs)

- No keyboard shortcuts. Ctrl+N/O/S/Z/Y are displayed as menu hints
  but the host doesn't bind them yet — same gap as Edit's Ctrl+Z/Y.
- No "save before close" confirmation. Quitting with a dirty document
  silently discards changes.
- No asset / LayerGraph contents in the archive yet — the zip
  container reserves `assets/` and `graphs/` subdirs but v1
  writers only emit `document.json`.
- No compositor wired into app/main.cpp yet (LayerCompositor exists with masking, but main still runs the textured-quad demo).
- No brush variety beyond the MVP black tip; presets / library TBD.
- Pen pressure plumbed on Windows; macOS / Linux still mouse.
- No persistence layer.
- No UI chrome (no widgets, no panels, no menus).
- File format MVP shipped (JSON + zip container); asset / graph / history embedding still pending.
- Edit coalescing not implemented (every keystroke is one undo entry — production-ready coalescing is a P4 follow-up).
- No tests for GPU code (CI has no GPU).

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

Expected: window opens with the 4-layer demo composite (dark
navy base → muted red → blue glow → warm tint), a Dear ImGui
demo window on top, and any pen / mouse drag deposits ink stamps
that survive across frames.

### Read first (in order — ~30 min)

1. **This file** — the "Where we are" + "Roadmap" sections above are
   the entry point. Skim "What does NOT work yet" to know what's
   intentionally absent vs broken.
2. [`docs/architecture/README.md`](docs/architecture/README.md) → the
   27 ADRs in numeric order. **Read all of them** before changing
   cross-cutting code. ADR 0001 (C++23 + Vulkan), 0003 (Result<T>),
   0004 (harness), 0011 (2026 baseline), 0016 (LayerGraph), 0019
   (compositor), 0023 (Document), 0024 (Command/Undo), and 0027 (UI
   stack) are the most-referenced; the rest fill in details.
3. [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch protocol, commit
   convention, code style.
4. [`README.md`](README.md) — project overview.

### Pick up where I left off

Sequential next steps from the roadmap:
**UI pivot is active.** Engine MVP is sufficient end-to-end;
further engine investment (P5 modern Vulkan, asset embedding in
file format, edit coalescing, color management) is premature
without a UI that lets us validate. ADR 0027 commits to Dear
ImGui for v0.x with an explicit phase boundary for v1.0
re-evaluation.

1. ~~**`feat/ui-file-menu-wire`**~~ ✅ landed (PR #45).
2. ~~**`fix/compositor-render-pass-init`**~~ ✅ landed (PR #46) — ADR 0028.
3. ~~**`feat/ui-cjk-font`**~~ ✅ landed (PR #47) — CJK font half of #18h.
4. ~~**`feat/ui-keyboard-shortcuts`**~~ ✅ landed (PR #49).
5. ~~**`feat/ui-dirty-confirm`**~~ ✅ landed (PR #50).
6. ~~**`feat/ui-debug-overlay`**~~ ✅ landed (PR #51).
7. **`feat/ui-theme-pass`** — Custom ImGuiStyle + dark/light theme.
   The CJK font half of this item shipped in PR #47; what remains is
   the colour scheme. ~3 h.
8. **`feat/ui-block-rename`** — first writable in-canvas widget. Pick
   a Text block in the outline → inline rename → AddBlockCommand's
   sibling `RenameBlockCommand`. Once this lands the
   `ImGuiInputFlags_RouteFocused` routing comment in #49 needs to be
   honored — Ctrl+Z inside the rename input should undo text, not
   the document.

**Deferred until UI validation:**
- P5 #13 `feat/shader-objects` (`VK_EXT_shader_object`) and
  the rest of P5 modern Vulkan extensions
- Asset / LayerGraph / history embedding in the `.noted` archive
- Edit coalescing in `UndoStack`
- macOS / Linux pen-input ports

---

## Roadmap — what to do next (priority order)

Each entry is a single focused PR. Estimated effort assumes a developer
familiar with Vulkan and C++. Pick from the top.

### 🔥 Priority 1 — Debt and observability

These unblock everything else. Do them before adding new features.

| # | PR | Effort | Why |
|---|---|---|---|
| 1 | ~~`feat/format-sweep`~~ ✅ **landed** | — | `clang-format-18` applied across all 118 `.hpp/.cpp` files (engine/domain/compositor/plugin/platform/ui/app/tests). CI lint job is now blocking. Local install via `uv tool install clang-format==18.1.8` or `pip install clang-format==18.1.8`. |
| 2 | ~~`feat/tracy-integration`~~ ✅ **landed** | — | Tracy via FetchContent + `NOTED_ENABLE_TRACY` option. `harness::ScopedTimer` → Tracy zones, `Counter` → Tracy plots. See ADR 0013. |

### 🎨 Priority 2 — Canvas + stroke (Goodnotes side)

The product's note-taking half. Each PR builds on the previous.

| # | PR | Effort | Depends on | Why |
|---|---|---|---|---|
| 3 | ~~`feat/canvas-render-target`~~ ✅ **landed** | — | — | `CanvasRenderTarget` (R8G8B8A8_UNORM, COLOR_ATTACHMENT\|SAMPLED\|TRANSFER_DST) + `Renderer::render_with_canvas` two-pass flow. Internal layout tracking via sync2 barriers. See ADR 0014. |
| 4 | ~~`feat/stroke-engine-mvp`~~ ✅ **landed** | — | — | SDF disk-stamp pipeline (`stamp.slang`) + `noted::stroke::StrokeEngine` (heap-allocated, non-movable, RAII hook subscriptions). Mouse drag → anti-aliased disks layered over the textured background. See ADR 0015. |
| 5 | ~~`feat/pen-input`~~ ✅ **landed** | — | — | Win32 `WM_POINTER` subclass over GLFW. Real pressure (0..1024 → [0, 1]) + tilt (degrees) flow through existing hook events. See ADR 0017. |
| 6 | ~~`feat/stroke-engine-pressure`~~ ✅ **landed** | — | — | `BrushStyle` (min/max radius, gamma alpha curve, softness ratio) + pure `stamp_from_pressure()` mapping. Live-tunable via `set_brush()`. See ADR 0018. |

### 🖼️ Priority 3 — Layers + blend (Photoshop side)

The image-editor half. Can be developed in parallel with strokes.

| # | PR | Effort | Why |
|---|---|---|---|
| 7 | ~~`feat/layer-domain-model`~~ ✅ **landed** | — | `domain::LayerGraph` — DAG of `LayerNode` (id/kind/blend/opacity/visible/inputs). 16-mode Photoshop blend enum + 5-kind layer enum, both wire-stable. Monotonic IDs, validate-then-mutate, cycle detection via iterative DFS. See ADR 0016. |
| 8 | ~~`feat/layer-compositor`~~ ✅ **landed** | New `compositor/` module bridging `engine` + `domain`. `LayerPayloadStore` (SolidColor MVP) + `LayerCompositor` with 4 fixed-function blend modes (normal/screen/linear_dodge/multiply) and counted fallback to NORMAL for the other 12. See ADR 0019. |
| 9a | ~~`feat/selection-domain`~~ ✅ **landed** | — | `domain::Selection` — canonical rect-list with union/intersect/subtract set ops, bounds, half-open `contains`. Same data/GPU split as LayerGraph→Compositor. See ADR 0020. |
| 9b | ~~`feat/selection-mask-gpu`~~ ✅ **landed** | — | `gpu::SelectionMask` (R8_UNORM, layout tracking) + `compositor::SelectionRasterizer` (CPU rasterize → buffer-to-image copy). Pure step unit-tested without a GPU. See ADR 0021. |
| 9c | ~~`feat/compositor-masking`~~ ✅ **landed** | — | `LayerCompositor::composite()` takes an optional `SelectionMask*`; fragment multiplies output by mask sample. 1×1 dummy keeps shader unconditional. See ADR 0022. |

### 📄 Priority 4 — Document model + persistence

| # | PR | Effort | Why |
|---|---|---|---|
| 10 | ~~`feat/document-block-tree`~~ ✅ **landed** | — | `domain::Document` — strict tree of `BlockNode` (group/text/heading/code/canvas/image/embed). Payload variant + opaque side-store IDs for heavy data. parent+children for O(1) both directions. 30 unit tests. See ADR 0023. |
| 11 | ~~`feat/command-undo-redo`~~ ✅ **landed** | — | `Command` abstract base + 7 concrete commands (add/insert/remove/move/set_payload/set_visible/set_name) + `UndoStack` (bounded depth, redo invalidation, peek labels). Inverse-based undo keeps stack memory tight. Adds `Document::restore_subtree` as the precise inverse of `remove_block`. 27 unit tests. See ADR 0024. |
| 12 | ~~`feat/file-format-mvp`~~ ✅ **landed** (JSON only) | — | `domain::io::document_to_json` / `from_json` round-trip the block tree. Strict parser, version-gated, integer kind ordinals (ADR 0023 wire-stable). 26 unit tests. nlohmann/json dep. See ADR 0025. |
| 12b | ~~`feat/file-format-zip`~~ ✅ **landed** | — | `.noted` is a standard ZIP via miniz. `platform::io::save_noted_file` / `load_noted_file` + a bytes-level API for in-memory use. 256 MB extraction cap. Reserved `assets/` and `graphs/` subdirs for follow-ups. 13 unit tests. See ADR 0026. |

### ⚡ Priority 5 — Modern Vulkan (post-MVP)

The 2026-trend extensions from ADR 0011. Lift to AAA-class scale once
the product has actual content.

| # | PR | Effort | Why |
|---|---|---|---|
| 13 | `feat/shader-objects` | 6h | `VK_EXT_shader_object` — pipeline-less shaders. Avoids combinatorial pipeline state explosion when we ship hundreds of brushes / filters. |
| 14 | `feat/descriptor-buffer` | 6h | `VK_EXT_descriptor_buffer` — pack descriptors into normal buffers. AAA-grade bindless. |
| 15 | `feat/mesh-shaders` | 8h | `VK_EXT_mesh_shader` — vector-graphics-style ink stroke rendering on the GPU. The right answer for high-stroke-count Goodnotes scenarios. |
| 16 | `feat/timeline-semaphore-renderer` | 4h | Replace fence + binary-semaphore sync with timeline semaphores (already enabled on Device). Simpler multi-queue code. |

### 🖥️ Priority 6 — UI

| # | PR | Effort | Why |
|---|---|---|---|
| 17 | ~~`feat/ui-stack-decision`~~ ✅ **decided** | — | ADR 0027 picks **Dear ImGui** (docking branch, MIT, official Vulkan+GLFW backends) for v0.x with an explicit phase boundary for v1.0 reassessment. Pure-design PR — no code change beyond the `ui/ui.hpp` docstring refresh. |
| 18a | ~~`feat/ui-imgui-scaffold`~~ ✅ **landed** | — | Dear ImGui docking v1.91.5 via FetchContent + official Vulkan/GLFW backends, ALL wrapped by `ui::ImGuiHost` with three-phase frame (`begin_frame` / `finalize_frame` / `render_into`). 6 unit tests cover create() rejection paths. See ADR 0027. |
| 18b | ~~`feat/ui-compositor-wire`~~ ✅ **landed** | — | `app/main.cpp` drives canvas pass via `LayerCompositor::composite()` walking a 4-layer demo LayerGraph (normal / multiply / linear_dodge). Textured-quad demo + checkerboard / sample.png loading retired. Stroke + ImGui still overlay correctly. |
| 18c | ~~`feat/ui-imgui-imassert-routing`~~ ✅ **landed** | — | `IM_ASSERT` routes through `noted::harness::validate` via `IMGUI_USER_CONFIG` + a forward-decl in `ui/include/noted/ui/imgui_user_config.hpp`. ADR 0027 follow-up closed. |
| 18d | ~~`feat/ui-document-shell`~~ ✅ **landed** (shell first cut) | — | Menu bar (File / Edit / View / About) + layer panel (visibility toggle wires through `set_visible()`) + status bar (frame index + FPS). `ShowDemoWindow` retired to View menu toggle. |
| 18d-undo | ~~`feat/ui-document-undo-outline`~~ ✅ **landed** | — | `Document` + `UndoStack` live in main.cpp. Outline panel renders `Document.preorder()` with click-to-select. Edit menu's Undo/Redo back the UndoStack live; Edit → Add Block submenu emits `AddBlockCommand` with proper parent selection (selected group → root → invalid_block_id). |
| 18d-file | ~~`feat/ui-file-menu-wire`~~ ✅ **landed** | — | File → New / Open / Save / Save As. nativefiledialog-extended via FetchContent. `platform::io::pick_noted_open` / `pick_noted_save` returns `Result<optional<path>>` (nullopt = user cancel). main.cpp tracks `current_path` + `saved_undo_size` for the title-bar dirty marker. Save force-falls-through to Save As when there's no backing path; Save As force-appends `.noted` if missing. |
| 18e | ~~`feat/ui-keyboard-shortcuts`~~ ✅ **landed** (PR #49) | — | Ctrl+N/O/S/Shift+S/Z/Y/Q via `ImGui::IsKeyChordPressed`. Save fall-through matches the menu; Undo/Redo gated by stack state. Routing defaults to `RouteGlobal`; flip to `RouteFocused` per-chord once text-input widgets land. |
| 18f | ~~`feat/ui-dirty-confirm`~~ ✅ **landed** (PR #50) | — | Modal "Save / Discard / Cancel" on window close (X / Quit / Ctrl+Q) + File → New on dirty. One state machine, `confirmed_exit` flag prevents the loop from re-prompting on Save success. Double-X-click race guarded. |
| 18g | ~~`feat/ui-debug-overlay`~~ ✅ **landed** (PR #51) | — | Floating window (View → Debug overlay; off by default): frame + FPS, 120-sample CPU-time line plot from `on_frame_end`, `LayerCompositor::fallback_count()`, name/value table of every `harness::Counter`. |
| 18h-font | ~~`feat/ui-cjk-font`~~ ✅ **landed** (PR #47) | — | OS-installed CJK TTF/TTC probed at startup (malgun.ttf / AppleSDGothicNeo / Noto Sans CJK KR / Nanum Gothic). `GetGlyphRangesKorean()` + 2048×2048 atlas. Graceful fallback to ProggyClean on any failure. |
| 18h-theme | `feat/ui-theme-pass` | 3h | Custom `ImGuiStyle` + dark/light palette. Defuses the "looks like debug tool" risk. |

---

## Known debt and gotchas

### Format drift — resolved
clang-format-18 is mandatory on every PR. Local setup:
`uv tool install clang-format==18.1.8` (or `pip install clang-format==18.1.8`).
The lint CI job blocks merges on drift; run
`clang-format-18 -i path/to/file.cpp` to fix.

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
Slang function name — see ADR 0012 (corrected) and `fix/slang-runtime-bugs`
commit for the painful learning.

### `pName "main"` for now
Once we have multiple compute shaders in the same .slang file, we'll
need either per-entry SPV files (current approach) or a way to keep
distinct entry names. The Slang spec allows `--entry-point-name` to
rename; if we go that route, update the rule.

### Hardcoded shader path
`NOTED_SHADER_DIR` is a compile-time define pointing at `build/shaders/`.
Production packaging will need a relocatable install layout
(`AppData/...` or similar). Out of scope for current PRs.

### Semaphore semantics
We use binary semaphores with per-image render_finished. When we
migrate to timeline semaphores (`feat/timeline-semaphore-renderer`),
the per-image structure changes. Read ADR 0008 and the recent semaphore
fix commit before touching `engine/src/gpu/renderer.cpp`.

---

## Useful commands

```powershell
# Clean rebuild
rm -r build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Open PR via gh CLI
gh pr create --base main --head <branch> --title "..." --body "..."

# Watch CI on a PR
gh pr checks <number> --watch

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

Good luck. The foundation is solid; the fun part starts at #3.
