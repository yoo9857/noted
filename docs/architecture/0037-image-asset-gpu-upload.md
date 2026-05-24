# ADR 0037 — Image asset GPU upload + ImTextureID registry

**Status:** Accepted. Implementation lands in `feat/image-gpu-upload`.
**Date:** 2026-05-24
**Builds on:** [ADR 0028 (compositor frame-safe init + descriptor rotation)](0028-compositor-frame-safe-init.md),
[ADR 0036 (`.noted` asset bundle)](0036-noted-asset-bundle.md).
**Closes phase:** B.7.b.2b — the last open slice of the image-tool
plan; image primitives now actually paint pixels.

## Context

Three slices stacked the image-tool path so far:

1. **B.7.b.1** (PR #98) introduced `ImageAssetRegistry` + `AssetId`
   stamps on every `ImagePrimitive`. Pure domain.
2. **B.7.b.2** (PR #100) wired the file picker to call
   `platform::image_io::load_rgba8`, capturing each picked image's
   intrinsic dimensions and stamping a fresh `AssetId`. The decoded
   RGBA buffer was discarded right after the dimension read.
3. **B.7.b.3** (ADR 0036 / PR #129) added `ImageAsset::source_bytes`
   carrying the encoded payload, plus `assets/<id>` zip members so
   the payload round-trips through save / load.

What was still missing: every image primitive on the canvas drew as
the dashed "Image" placeholder. The bytes existed in domain + on
disk, but no path turned them into a sampled Vulkan texture. This
ADR closes the loop.

## Decision

### Module: `compositor::ImageAssetGpuRegistry`

Place the registry in **compositor** (the engine + domain bridge).
The asset id space + encoded payload live in domain; the GPU
resources live in engine; the registry's job is to keep them in
sync. That's the same shape as `compositor::LayerCompositor` and
`compositor::SelectionRasterizer`, so the layering decision is
already settled.

### Lifecycle: synchronous decode + upload + ImGui descriptor set

For each domain asset with non-empty `source_bytes` and no
corresponding GPU entry, the per-frame `sync_image_assets` bridge:

1. Calls `platform::image_io::decode_rgba8(asset.source_bytes)` —
   stb_image memory variant added alongside this PR.
2. Allocates a `noted::gpu::Image` (VMA-backed `R8G8B8A8_UNORM`).
3. Calls `noted::gpu::upload_image_pixels` — synchronous staging
   upload that blocks until the GPU has consumed the bytes and
   the final `SHADER_READ_ONLY_OPTIMAL` transition has retired.
4. Calls `ImGui_ImplVulkan_AddTexture(sampler_, view, layout)` to
   register a descriptor set with ImGui's Vulkan backend; the
   returned `VkDescriptorSet` is the `ImTextureID` the overlay
   uses for `AddImage`.

The whole thing happens on the main thread. A 4K JPG decode +
upload can stall a frame for tens of ms — acceptable for v0.x's
one-image-at-a-time picker flow. An async / threaded decoder
belongs in a follow-up (`feat/async-asset-loading`).

### Frame-in-flight safety: delayed destruction

`upload` (when it replaces an existing entry) and `remove` push the
old entry onto a `retired_` queue with a `release_after_tick`
counter equal to `frame_counter_ + kRetireDelayFrames` (3 — covers
the swapchain's `frames_in_flight = 2` plus one margin frame).

`tick()` (called once per frame, alongside the sync) increments
the counter and releases any retired entries whose delay has
elapsed. By that point no in-flight command buffer can still
reference the descriptor set, so `ImGui_ImplVulkan_RemoveTexture`
and the `gpu::Image` destructor are safe to run.

This mirrors the discipline ADR 0028 set up for `LayerCompositor`'s
per-frame-in-flight descriptor rotation.

### Single shared LINEAR sampler

One `VkSampler` is created at registry construction time
(LINEAR/LINEAR, CLAMP_TO_EDGE) and shared across every asset's
descriptor set. Per-asset filter settings (NEAREST for pixel art,
anisotropic, etc.) are deferred — the codebase doesn't yet have a
UI surface for them and ImGui's `AddTexture` rebinding is cheap
enough that switching the sampler later doesn't trap us into a
schema migration.

### Overlay path: textured first, placeholder fallback

`ui::widget::image_overlay`'s signature grows a third callback,
`ImageTextureLookupFn`: `AssetId → uint64_t`. The lookup returns 0
to mean "no GPU texture — please draw the placeholder", which
keeps every existing failure mode (no asset_id, decode failed,
v9 file without bundled bytes, decode in flight) on the same
visible fallback path. When the lookup returns non-zero, the
overlay draws via `ImGui::AddImage` with the per-primitive RGBA
tint applied at the draw-list level so a slider tweak doesn't
re-upload pixels.

Header includes `<cstdint>` rather than `<imgui.h>`: the callback's
return type is `std::uint64_t` (which the overlay impl casts to
`ImTextureID`). Keeps the overlay's caller side free of imgui
transitively.

### Decode happens on the bridge side, not in the registry

`ImageAssetGpuRegistry::upload(...)` takes already-decoded RGBA
bytes. The bridge function `sync_image_assets` is the one that
calls `platform::image_io::decode_rgba8`. This split:

- Keeps the registry's API testable against raw byte buffers
  (no platform / decode dep in the public surface).
- Lets a future async decoder feed the registry from a worker
  thread — the registry still sees only "here are some
  pre-decoded pixels, please upload" and nothing about decode
  policy changes here.

### Rejected alternatives

- **Hold registry in engine layer, expose VkImage** —  splits the
  imgui descriptor-set lifecycle across two modules (engine creates
  the image, app calls `ImGui_ImplVulkan_AddTexture`). The registry
  is exactly where they pair up; co-locating is simpler.
- **Per-asset sampler** — over-engineering for v0.x with no UI to
  drive it.
- **No retire queue — `vkDeviceWaitIdle` on every remove** —
  correct but introduces a multi-millisecond stall every time
  the user removes an image. Delayed destruction is cheap and
  matches the rest of the engine's frame-in-flight discipline.
- **Decode in the registry** — couples the GPU layer to
  stb_image and pessimises any future move-to-thread refactor.
- **Pre-decode every asset at load time** — adds wall-clock to
  every `.noted` open and burns memory on assets the user may
  never bring into view. Lazy upload (on first frame an
  unbound asset appears) is fine.

## Implementation surface

**`platform/image_io`**:
- `decode_rgba8(std::span<const std::byte>) -> Result<LoadedImage>`
  — in-memory variant alongside the existing file-path `load_rgba8`.
  Shared finalize helper consumes both stb_image return paths.

**`compositor/`**:
- `image_asset_gpu_registry.{hpp,cpp}` — `ImageAssetGpuRegistry`
  class + `sync_image_assets` bridge.
- `CMakeLists.txt` gains `noted::platform` and `imgui` as
  `PUBLIC_DEPS` (the bridge calls into platform's decoder; the
  registry uses ImGui's Vulkan backend).

**`ui/widget/image_overlay`**:
- Signature gains `const ImageTextureLookupFn&` (third arg).
- When the lookup returns non-zero, draws via `AddImage` with the
  per-primitive RGBA tint. When zero, falls back to the existing
  placeholder triad.

**`app/src/app`**:
- New member `std::optional<ImageAssetGpuRegistry> image_assets_gpu_`,
  constructed after `imgui_host_` in `init_imgui`.
- `on_frame` calls `sync_image_assets(...)` + `tick()` between
  `imgui_host_->begin_frame` and the workspace dockspace
  (BEFORE any draw lists are populated).
- `UiPanels::Deps::image_texture_lookup` callback installed
  alongside `on_pick_image`; returns 0 when the registry hasn't
  been built yet (transient during init).

**`ui_panels`** (`app/src/ui/`):
- `Deps::image_texture_lookup` field + member;
  `image_overlay` call grows the third arg.

**Tests** (`tests/unit/image_io_test.cpp` — new file, 4 cases):
- `decode_rgba8` empty buffer → error.
- `decode_rgba8` random bytes → error.
- `decode_rgba8` of a hand-built 1×1 BMP succeeds with width=1,
  height=1, pixels.size()==4.
- Decoded pixel channels round-trip BGR→RGBA correctly with
  full opaque alpha.

The GPU upload + registry + sync path itself can't be exercised
in CI (no GPU on the runners). Verified locally on the same
GTX 1050 Ti the rest of the engine targets.

## Migration

- **No domain or schema change.** The contract `ImageAsset` /
  `Document` set in ADR 0036 stays bit-for-bit identical; this PR
  only adds a runtime reader of `source_bytes`.
- **No `.noted` schema bump** — v10 is the current version and
  carries all the data this PR consumes.
- **Behavioural change visible to users**: picked images now paint
  real pixels instead of a dashed rectangle, both on the picker
  frame and on every subsequent re-open of a saved `.noted`.

## Out of scope (deliberate)

- **Async / threaded decode**. A 4K decode on the UI thread is a
  perceptible stall; `feat/async-asset-loading` will move it to
  a worker.
- **Mipmaps**. Single full-res mip, LINEAR filter. Mipmap
  generation lands alongside the upload-quality pass.
- **Sampler customisation** (NEAREST for pixel art, anisotropy).
  No UI surface yet.
- **Asset reference counting / garbage collection**. The
  reconcile bridge currently retires GPU entries when the
  corresponding domain asset disappears; we don't yet detect
  "no `ImagePrimitive` references this asset, so drop it from
  domain too". That's a separate cleanup PR.
- **Cropping / rotation / shear**. Same blockers as the
  placeholder era — they need their own tool affordance before
  this matters.
