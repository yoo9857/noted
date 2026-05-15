# ADR 0001: C++23 + Vulkan as the engine core

**Status:** Accepted
**Date:** 2026-05-15

## Context

We are building a professional raster image editor + note-taking application
with the explicit goal of matching or exceeding Photoshop on performance and
correctness. The engine has to:

- composite tens of layers at 4K-8K resolution at 60+ FPS;
- run pixel-accurate filters with deterministic color management;
- ship on Windows (primary), macOS, and Linux;
- live for 5-10+ years.

## Decision

The engine core is written in **C++23** and renders through native GPU APIs:
**Vulkan 1.3** on Windows and Linux, **Metal 3** on macOS, with a thin
abstraction layer in `engine/gpu/`. Shaders are authored in **Slang**.

## Alternatives considered

- **Rust + wgpu.** Memory safety is attractive and the toolchain is modern.
  Rejected because (1) `wgpu` adds an indirection over Vulkan/Metal that costs
  meaningful frame time at high layer counts; (2) CUDA, Optick, and the
  vendor SDKs (Nsight, RenderDoc plugins, NVIDIA OptiX) are C++-first; (3)
  borrow-checker patterns occasionally force suboptimal layouts in image
  pipelines (e.g. shared mutable tile caches).
- **Electron / Flutter / Qt + JS.** Garbage collection causes brush latency
  spikes at high resolutions. Out of scope for AAA.
- **C# / .NET.** Windows-first ecosystem; weaker GPU compute story.
- **One layer above Vulkan (e.g. Diligent, sokol_gfx).** Adds indirection
  without saving meaningful code — the abstraction we write ourselves is small.

## Consequences

- We accept the cost of writing platform-specific GPU code three times.
  The `engine/gpu/` interface is small enough (≤30 verbs) to make this
  tractable, and each backend buys us first-class profiling and debugging.
- We accept the C++ memory-safety cost. To mitigate: strict warning policy,
  clang-tidy enforcement, ASan/UBSan in CI, no raw `new`/`delete` outside
  the allocators in `engine/memory/`, and `Result<T>` instead of exceptions.
- Hiring pool narrows. Mitigated by the fact that AAA creative software
  applicants overwhelmingly come from C++ backgrounds.
