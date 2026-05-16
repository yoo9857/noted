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
| 0018 | [Pressure-driven stamps in the stroke engine](0018-stroke-engine-pressure.md) | Accepted |

## Format

Each ADR has four sections: **Context**, **Decision**, **Alternatives**,
**Consequences**. Keep it short — the goal is a 5-minute read that explains
why a future contributor shouldn't undo the work.
