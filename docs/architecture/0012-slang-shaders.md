# ADR 0012: Slang is the only shader language

**Status:** Accepted
**Date:** 2026-05-15

## Context

We started on GLSL because it's the historic Vulkan baseline and `glslc`
ships in every Vulkan SDK. By 2025 the picture had shifted: Microsoft
and Khronos jointly drive Slang (an HLSL-derived language with modules,
generics, and multiple compilation targets), and the Vulkan SDK 1.4
bundles `slangc` directly. Keeping two shader languages or doing a
half-migration is the kind of legacy that compounds. Pick one, commit.

## Decision

**Slang is the only shader language. GLSL files and glslc are removed.**

- All shader source lives in `.slang` files under `shaders/`.
- One `.slang` file may declare multiple entry points using the
  `[shader("vertex")]` / `[shader("fragment")]` / `[shader("compute")]`
  attributes. `slangc` is invoked once per entry point, producing one
  SPIR-V module per stage.
- The SPIR-V entry-point name matches the Slang function name (e.g.
  `vs_main`, `ps_textured`). The `GraphicsPipelineBuilder::add_stage`
  call takes the entry name explicitly.
- `cmake/Shaders.cmake` exposes `noted_compile_slang_entry(SOURCE,
  ENTRY, OUTPUT[, PROFILE])`. Profile defaults to `spirv_1_5` (matches
  the engine's Vulkan 1.4 + `dynamicRendering` baseline).
- `find_program(slangc HINTS ENV VULKAN_SDK)` makes the build hard-fail
  if the SDK is too old. No silent fallback to glslc.

Example `shaders/fullscreen.slang` declares three entries:

```slang
struct VOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID) { ... }

[shader("fragment")]
float4 ps_gradient(VOut input) : SV_Target { ... }

[[vk::binding(0, 0)]]
Sampler2D u_image;

[shader("fragment")]
float4 ps_textured(VOut input) : SV_Target {
    return u_image.Sample(input.uv);
}
```

Compiled to three `.spv` files; the app loads the pair it needs at
pipeline-build time.

## Why Slang specifically

- **Industry direction.** Microsoft donated Slang to Khronos in 2024;
  it's the official cross-platform shader language going forward.
- **Modules + generics.** Real reusable code in shaders for the first
  time. Filters, blend modes, brush math — all shareable without macro
  abuse.
- **Multi-target.** SPIR-V today, MSL / HLSL / CUDA / WGSL future. The
  Photoshop-side filters we'll ship can later run on Metal or DirectX
  without rewriting.
- **Multiple entry points per file.** Co-locating vertex + fragment
  removes the GLSL pain of two files staying in sync. Critical for the
  brush engine where every brush has a tightly-coupled vert + frag pair.
- **Better diagnostics.** Slang's error messages are HLSL-class. GLSL
  errors are notoriously cryptic.

## Alternatives considered

- **Keep GLSL.** Familiar, but stagnant. Loses access to modules and the
  cross-target story; we'd have to add HLSL or MSL ourselves later.
- **HLSL via DXC.** Same target reach as Slang minus the modules and
  generics. Microsoft itself is steering its long-term roadmap into
  Slang.
- **Mix GLSL and Slang.** Half-migration is worse than either choice
  alone. Reviewed and rejected.
- **WGSL.** Web-target focused; not the right primary for a desktop
  engine even if we eventually ship a WASM build.

## Consequences

- `glslc` no longer needed. The Vulkan SDK still bundles it but we don't
  call it.
- Vulkan SDK floor is bumped to 1.4.x (already done in ADR 0011).
- Shader file count drops: one `.slang` file replaces what would have
  been one `.vert` + one or more `.frag` files per pipeline.
- All renderer code passes explicit entry-point names to
  `GraphicsPipelineBuilder::add_stage`. The previous `"main"` default is
  harmless but only useful for legacy GLSL output.
- Adding a new pipeline type is now: declare a new entry in an existing
  `.slang` file (or create a new one), add `noted_compile_slang_entry`
  to `shaders/CMakeLists.txt`, load the resulting `.spv`. No tooling
  changes.
