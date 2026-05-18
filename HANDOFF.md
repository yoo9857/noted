# Handoff — where the project is and what's next

**Last updated:** 2026-05-16 · **main HEAD:** `bc457e7` (clean, 0 open PRs)

Goal: a professional note-taking + raster image editor that exceeds
Goodnotes (vector ink, stylus-first) AND Photoshop (raster layers,
filters) in one unified document. AAA-grade performance, maintainability,
exception handling.

---

## Where we are

**The app builds and runs.** A 1600×1000 window opens, the GPU is picked,
a Slang-compiled fullscreen quad samples a procedural checkerboard
texture, the frame loop ticks at ~3960 FPS on a GTX 1050 Ti with **zero
Vulkan validation errors**.

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
docs/architecture/  20 ADRs documenting every cross-cutting decision
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
✅ Build hygiene: zero MSVC warnings on Release. Third-party headers
   (GLFW/VMA/stb/Tracy/GoogleTest) marked SYSTEM via FetchContent so
   their warnings can't leak. `/Ob[0-9]` collisions removed at the
   cache layer.
✅ CI matrix verifies build + sanitizers + Tracy smoke build on every PR.

### What does NOT work yet (by design — not bugs)

- No actual document model (block tree — P4 #10).
- No GPU selection mask yet (domain ships first — see ADR 0020).
- No brush variety beyond the MVP black tip; presets / library TBD.
- Pen pressure plumbed on Windows; macOS / Linux still mouse.
- No persistence layer.
- No UI chrome (no widgets, no panels, no menus).
- No file format.
- No undo/redo wired (Command interface exists; no stack yet).
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

Expected: window opens with magenta/grey checkerboard. Drop any
`sample.png` next to `noted_app.exe` to replace the checkerboard with a
real image.

### Read first (in order — ~30 min)

1. **This file** — the "Where we are" + "Roadmap" sections above are
   the entry point. Skim "What does NOT work yet" to know what's
   intentionally absent vs broken.
2. [`docs/architecture/README.md`](docs/architecture/README.md) → the
   20 ADRs in numeric order. **Read all of them** before changing
   cross-cutting code. ADR 0001 (C++23 + Vulkan), 0003 (Result<T>),
   0004 (harness), 0011 (2026 baseline), 0016 (LayerGraph), and 0019
   (compositor) are the most-referenced; the rest fill in details.
3. [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch protocol, commit
   convention, code style.
4. [`README.md`](README.md) — project overview.

### Pick up where I left off

Sequential next steps from the roadmap:
1. **P3 #9c** `feat/compositor-masking` — `LayerCompositor::composite()`
   takes an optional `SelectionMask&`, layer shader multiplies output
   alpha by mask sample. ~3 h.
2. **P4 #10** `feat/document-block-tree` — independent of #9c.
   Pure domain logic, follows the same `domain::LayerGraph` pattern.

If you're new to the codebase, P4 #10 is the gentlest landing —
no GPU work, no shaders, well-bounded.

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
| 9c | `feat/compositor-masking` | 3h | `LayerCompositor::composite()` takes an optional `SelectionMask`; layer shader multiplies output alpha by mask sample. Builds on 9b. |

### 📄 Priority 4 — Document model + persistence

| # | PR | Effort | Why |
|---|---|---|---|
| 10 | `feat/document-block-tree` | 6h | `domain::Document` block tree: text + canvas + image + embed blocks. Unified data structure for notes AND image editor. |
| 11 | `feat/command-undo-redo` | 4h | Command pattern + undo stack on top of the block tree. Every state mutation goes through `Command::apply()`. |
| 12 | `feat/file-format-mvp` | 4h | `.noted` archive format (zip-ish): document.json + assets/*.png + history.bin. Round-trip save/load. |

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
| 17 | `feat/ui-stack-decision` | research | ADR 0013: pick the UI stack. **Decision pending**: Qt 6 / custom IMGUI / Slint / Tauri webview. Each has trade-offs documented in ADR 0001's alternatives table. |
| 18 | `feat/ui-debug-overlay` | 4h | First UI surface: ImGui (or chosen stack) overlay showing FPS, harness counters, flag toggles. Bridges to the rest of the app via hook channels. |

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

### Stale `class Pipeline;` forward decl
Removed earlier; if it reappears, delete it. Forward declarations for
types that have real headers are dead code.

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
