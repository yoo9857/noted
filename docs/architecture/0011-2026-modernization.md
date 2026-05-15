# ADR 0011: 2026 modernization baseline

**Status:** Accepted
**Date:** 2026-05-15

## Context

After landing the Vulkan stack, descriptor sets, pipeline builder, and the
first textured fullscreen quad, the engine has enough surface area that
the choice of "what does 2026 look like for us" needs to be written down.
Without an explicit baseline, contributors guess — and the guesses pick
up cruft that slows the engine for a decade.

This ADR captures the toolchain versions, mandatory Vulkan features, and
near-term modernization roadmap as of 2026 Q2.

## Decision

### Baseline versions (2026 Q2)

| Component | Version | Why |
|---|---|---|
| C++ | C++23 | `std::expected`, `std::print`, modules in MSVC/clang/gcc. C++26 is partial across compilers; revisit late 2026. |
| MSVC | 17.10+ (Build Tools 2022) | Stable C++23 + modules |
| CMake | 3.28 min, 3.31 recommended | C++23 modules support, generator expressions we rely on |
| Ninja | 1.12+ | Standard fast generator |
| Vulkan SDK | **1.4.309.0** | 1.4 is the current Khronos baseline; 1.3 is now N-1 |
| GLFW | 3.4 | Latest stable |
| VMA | v3.1.0 | Current AMD GPUOpen release |
| stb_image | pinned commit | Stable single-header decoder |
| GoogleTest | v1.15+ | Latest stable |

### Mandatory Vulkan 1.2 + 1.3 features (the "2026 baseline")

Every `gpu::Device::create` enables the following by default. Backends and
tests can disable them via flags for portability experiments, but the
renderer assumes they are on:

**1.3 (already in `feat/gpu-swapchain`)**
- `dynamicRendering` — no `VkRenderPass`, no `VkFramebuffer`. Pipelines
  bind to attachments by format at draw time. See ADR 0007 / 0010.
- `synchronization2` — `VkPipelineStageFlags2`/`VkAccessFlags2` everywhere.
  Old sync API is forbidden in new code.

**1.2 (this ADR adds them as defaults)**
- `descriptorIndexing` + non-uniform sampled/storage indexing,
  `descriptorBindingPartiallyBound`, `descriptorBindingUpdateAfterBind*`,
  `descriptorBindingUpdateUnusedWhilePending`, `runtimeDescriptorArray`,
  `descriptorBindingVariableDescriptorCount`.
  This is the bindless foundation. Layers, textures, and brush stamps
  will live in big descriptor arrays indexed by GPU.
- `bufferDeviceAddress` — pointer-as-uniform shader access. Required by
  ray-traced compositing and by GPU-driven indirect draws.
- `timelineSemaphore` — single primitive replaces fences + binary
  semaphores. Will replace `FrameSync`'s fence+two-semaphore design
  once the renderer needs multi-queue submits.

These features are **required** on every supported GPU. If a card
doesn't expose them, `Device::create` fails. Practically: every NVIDIA
Turing / AMD RDNA / Intel Xe device from 2019 onward supports them.

### Roadmap (already-pinned follow-ups)

| PR | Goal |
|---|---|
| `feat/format-sweep` | Apply clang-format-18 across the codebase; flip the CI lint job back to `continue-on-error: false`. |
| `feat/slang-shaders` | Add Slang (Microsoft + Khronos) as the primary shader compiler. Keep glslc as the fallback. Slang's modules + generics + multi-backend (SPIR-V, MSL, HLSL) is the 2026+ story. |
| `feat/tracy-integration` | Tracy via FetchContent + `NOTED_ENABLE_TRACY` option. `harness::ScopedTimer` and `harness::Counter` route into Tracy zones / plots when enabled. |
| `feat/shader-objects` | `VK_EXT_shader_object` — pipeline-less shaders. Avoids the combinatorial explosion of pipeline state objects when we ship hundreds of brushes and filters. |
| `feat/descriptor-buffer` | `VK_EXT_descriptor_buffer` — pack descriptors into ordinary `VkBuffer`s, removing the per-descriptor-update cost. AAA-grade bindless. |
| `feat/pen-input` | Per-platform pen / tablet input (Pointer Input API on Windows, NSEvent on macOS, libinput on Linux) for pressure and tilt. |
| `feat/mesh-shaders` | `VK_EXT_mesh_shader` for vector-graphics-style ink stroke rendering. Big-win path for Goodnotes-style strokes. |

### CI policy update

A new sanitizer job runs every build with ASan + UBSan under clang-18 on
Linux. Catches use-after-free, double-free, UB in fundamental ops. Does
not run tests (no GPU on hosted runners); link-only smoke build.

clang-format CI is `continue-on-error: true` until `feat/format-sweep`
lands. The job still surfaces every drift as a warning so we never lose
visibility.

## Alternatives considered

- **Stay on Vulkan 1.3.** 1.3 is fine but 1.4 has been the official baseline
  for a year. We'd just be N-1. Rejected.
- **Skip descriptor indexing, design our own bindless.** Reinventing core
  Vulkan features wastes a decade. Rejected.
- **Adopt C++26 / std::execution early.** Partial compiler support across
  MSVC/clang/gcc; revisit Q4 2026 when one of them ships full P2300.
- **Switch to Bazel.** Better for monorepo scale; CMake is still the
  industry standard. Revisit at 100k+ LOC.

## Consequences

- Every new code path can assume bindless-friendly descriptors. New
  rendering features stop fighting the descriptor-set-per-frame pattern.
- Operators see ASan / UBSan failures in CI before they reach `main`,
  not after. Tightens the iteration loop.
- The roadmap is now public. Contributors know what's coming in 30–90 days
  and can build against it rather than blocking on it.
