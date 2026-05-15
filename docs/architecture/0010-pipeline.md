# ADR 0010: Pipeline conventions — dynamic rendering + builder

**Status:** Accepted
**Date:** 2026-05-15

## Context

A Vulkan graphics pipeline carries ~80 fields across half a dozen structs.
Most are noise for the renderer's common cases (triangle list, no blending,
one color attachment, dynamic viewport). We need a small ergonomic surface
that defaults to those common cases and lets the rendering code state only
what is unusual.

## Decision

### No VkRenderPass — use `VK_KHR_dynamic_rendering` (core in 1.3)

Every pipeline is built with `VkPipelineRenderingCreateInfo` chained via
`pNext`. The renderer states the color/depth/stencil **formats** at
pipeline build time and **attachments** at draw time. No `VkRenderPass`,
no `VkFramebuffer`, no subpass dependencies.

Consequences:
- Pipelines bind to any attachment with a matching format. Survives
  swapchain rebuild without re-creating pipelines.
- No `vkCmdBeginRenderPass` / `vkCmdEndRenderPass`. Drawing uses
  `vkCmdBeginRendering` / `vkCmdEndRendering`.
- Compatible with the Vulkan 1.3 baseline the engine already enables
  (`VkPhysicalDeviceVulkan13Features::dynamicRendering = VK_TRUE`).

### Fluent builder for `GraphicsPipeline`

```cpp
auto pipeline = noted::gpu::GraphicsPipelineBuilder{}
    .add_stage(VK_SHADER_STAGE_VERTEX_BIT,   vert)
    .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)
    .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    .color_format(swapchain.summary().color_format)
    .build(device, layout);
```

Defaults applied at construction:
- triangle list, back-face cull, CCW front
- single color attachment, no blending, full RGBA write mask
- 1× MSAA, depth test off
- dynamic state: `VIEWPORT`, `SCISSOR` (so resize doesn't invalidate
  the pipeline)

The builder is a value type. It's intentionally not move-only —
callers may keep a templated "base config" builder and `.build()` it
twice with different shaders.

### Viewport / scissor are always dynamic

Baking window size into the pipeline state means every resize forces
a pipeline rebuild. With dynamic viewport/scissor the renderer just
records `vkCmdSetViewport` / `vkCmdSetScissor` each frame.

## Alternatives considered

- **Keep VkRenderPass for explicit subpass control.** Subpasses are
  useful on mobile tilers (TBDR) for fused attachments. Desktop GPUs
  largely ignore them. We're desktop-first; revisit when we ship to
  iPadOS / Android.
- **Static viewport / scissor.** Saves two CmdSet calls per frame; loses
  resize robustness. Not worth it.
- **Code generation (e.g. via PipelineConfigDescriptor JSON).** Tempting
  for tooling but adds build complexity. Reserve for the day we have
  >50 pipelines.
- **`vk::raii` builder.** Same trade-off as ADR 0006 — defer.

## Consequences

- All pipelines use the same code path; the renderer can binary-diff
  two pipelines by inspecting builder state.
- Shader hot-reload is cheap: rebuild a single pipeline, swap the handle.
- Mobile port (TBDR) will require revisiting; called out explicitly in
  the roadmap.
