# Architecture Decision Records

ADRs capture cross-cutting decisions: why we chose X, what we rejected, what
the consequences are. They are append-only — when a decision changes, write a
new ADR that supersedes the old one rather than rewriting history.

| #    | Title                                  | Status   |
|------|----------------------------------------|----------|
| 0001 | [C++23 + Vulkan as the engine core](0001-cpp23-vulkan.md) | Accepted |
| 0002 | [Hook system: typed channels with priorities](0002-hook-system.md) | Accepted |
| 0003 | [Error handling: `Result<T>` instead of exceptions](0003-error-handling.md) | Accepted |
| 0004 | [Runtime harness for flexible control](0004-harness.md) | Accepted |
| 0005 | [Module layout: layered C++ libraries](0005-module-layout.md) | Accepted |
| 0006 | [Vulkan resource ownership: move-only RAII](0006-vulkan-resource-raii.md) | Accepted |
| 0007 | [Swapchain policy: format, present mode, resize](0007-swapchain-policy.md) | Accepted |
| 0008 | [Frames-in-flight policy](0008-frames-in-flight.md) | Accepted |
| 0009 | [Memory allocator + upload model](0009-memory-allocator.md) | Accepted |
| 0010 | [Pipeline conventions: dynamic rendering + builder](0010-pipeline.md) | Accepted |
| 0011 | [2026 modernization baseline](0011-2026-modernization.md) | Accepted |
| 0012 | [Slang is the only shader language](0012-slang-shaders.md) | Accepted |
| 0013 | [Tracy for frame-grained profiling](0013-profiling.md) | Accepted |
| 0014 | [Canvas render target — two-pass composition](0014-canvas-render-target.md) | Accepted |
| 0015 | [Stroke engine — MVP disk-stamp pipeline](0015-stroke-engine-mvp.md) | Accepted |
| 0016 | [Layer graph — the Photoshop side's data backbone](0016-layer-domain-model.md) | Accepted |
| 0017 | [Pen / stylus input via Windows Pointer API](0017-pen-input.md) | Accepted |
| 0018 | [Pressure-driven stamps in the stroke engine](0018-stroke-engine-pressure.md) | Accepted |
| 0019 | [Layer compositor — GPU render of the layer graph](0019-layer-compositor.md) | Accepted |
| 0020 | [Selection domain — rect-list set algebra](0020-selection-domain.md) | Accepted |
| 0021 | [Selection mask — GPU R8 image + buffer-to-image rasterizer](0021-selection-mask-gpu.md) | Accepted |
| 0022 | [Compositor masking — selection mask gates layer output](0022-compositor-masking.md) | Accepted |
| 0023 | [Document — unified block tree](0023-document-block-tree.md) | Accepted |
| 0024 | [Command + undo / redo on `Document`](0024-command-undo-redo.md) | Accepted |
| 0025 | [Document JSON serialization (v1 of the `.noted` format)](0025-document-json-format.md) | Accepted |
| 0026 | [`.noted` archive — zip container for the file format](0026-noted-archive-container.md) | Accepted |
| 0027 | [UI stack — Dear ImGui for v0.x, with a swap path to native chrome](0027-ui-stack-selection.md) | Accepted |
| 0028 | [Compositor frame-safe init + descriptor rotation](0028-compositor-frame-safe-init.md) | Accepted |
| 0029 | [Vector ink — polyline rendering](0029-vector-ink-polyline.md) | Accepted |
| 0030 | [Runtime config — typed `AppConfig` for user-facing tunables](0030-runtime-config.md) | Accepted |
| 0031 | [Tool state machine + canvas pass strategy for editing tools](0031-tool-state-machine.md) | Accepted |
| 0032 | [App layer responsibility decomposition (R.1–R.4)](0032-app-layer-decomposition.md) | Accepted |

## Format

Each ADR has four sections: **Context**, **Decision**, **Alternatives**,
**Consequences**. Keep it short — the goal is a 5-minute read that explains
why a future contributor shouldn't undo the work.
