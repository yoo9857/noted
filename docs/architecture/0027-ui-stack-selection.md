# ADR 0027: UI stack — Dear ImGui for v0.x, with a swap path to native chrome

**Status:** Accepted
**Date:** 2026-05-18

## Context

Every domain + GPU primitive needed for the product is in place:
LayerGraph + Compositor (ADR 0016 / 0019), CanvasRenderTarget
(0014), Selection + GPU mask + compositor masking (0020 / 0021 /
0022), Document + Command + UndoStack (0023 / 0024),
`.noted` JSON + zip file format (0025 / 0026). And yet
`app/src/main.cpp` still runs the **textured-quad demo** from
the engine's earliest commits — the compositor is not wired into
a real frame loop, there is no menu / panel / toolbar surface,
and the user has no way to interact with their Document beyond
the pen-input → stroke-engine path.

The blocker is **the UI stack decision**. We deliberately
deferred it through ADRs 0001 (which lists UI as "deferred"),
0014, and the current `ui/` module scaffold's docstring:

> Long-term we may swap implementations (Qt QML vs custom IMGUI
> vs Slint) without touching domain or engine.

That option-keeping has paid off — every domain layer is GPU- and
UI-agnostic, and every cross-cutting concern (undo, file format,
selection) already works without any UI. Now we have to commit.

## Decision

**Adopt Dear ImGui** (docking branch) as the v0.x UI stack:

- **License:** MIT, commercial-friendly.
- **Backend:** `imgui_impl_vulkan` + `imgui_impl_glfw` (both
  officially maintained, match our existing stack exactly).
- **Integration:** ImGui renders into our existing Vulkan command
  buffers, sharing the swapchain image with `LayerCompositor`. No
  parallel renderer, no separate event loop.
- **`ui/` module's existing 4-area split is preserved:**
  - `view/` — render of domain state via ImGui calls.
  - `widget/` — reusable composites (panels, properties, layer
    list, document outline).
  - `theme/` — custom ImGuiStyle + font atlas tuned for the
    "professional ink + raster editor" aesthetic, not the
    debug-tool default.
  - `binding/` — one-way subscription from domain hook channels
    into view, so the UI re-renders when Document /
    UndoStack / Selection change.

This is the right choice for v0.x; it is **not** locked in for
v1.0. See the §"Phase boundary" section below.

### Why ImGui (over the alternatives)

| Stack | License | Vulkan native | Canvas-first | Ink-latency | Iteration speed | Dep weight | C++ ABI | "Looks like product" out of the box |
|---|---|---|---|---|---|---|---|---|
| **Dear ImGui** (chosen) | MIT | yes (official) | ✓ shares cmd buffers | ✓ same-frame | fast (immediate mode) | small (single header) | clean | ✗ requires theming |
| Qt 6 | LGPL/commercial | no (QRhi or separate) | ⚠ retained mode owns the event loop | ⚠ Qt event loop adds latency | slow (moc + retained) | huge (50+ MB) | clean | ✓ native widgets |
| Slint | MIT/GPL/commercial | yes | ⚠ retained, custom canvas integration | ✓ once integrated | medium (DSL + recompile) | medium | clean | ✓ modern out of box |
| Tauri (webview) | MIT | no (separate process) | ✗ IPC for every event | ✗ webview latency | fast (web tech) | huge (~200 MB baseline) | FFI to Rust + JS | ✓ web look |
| Custom IMGUI on Vulkan | ours | yes | ✓ | ✓ | n/a (we write everything) | zero | n/a | ✗ 6-12 months of work |

**The deciding criteria for v0.x:**

1. **Canvas-first workload** — the dominant UI surface is a
   `LayerCompositor` render, not a form. Immediate-mode UIs sit
   "inside" the same render loop; retained-mode toolkits own the
   loop and impose latency between input and ink.
2. **Vulkan integration** — Dear ImGui's official Vulkan backend
   accepts a `VkCommandBuffer` and renders into it. Our
   compositor already accepts the same. They compose without a
   second renderer or shared-state ceremony.
3. **Time-to-first-frame** — we need a window with menus + a
   canvas + a layer panel **this month** to validate the rest of
   the architecture. ImGui's reputation for "demo on day 1" is
   real; Qt's setup is days, custom UI is months.
4. **License hygiene** — MIT keeps every downstream option open
   (commercial product, plugin SDK redistribution, embedded
   integrations). LGPL imposes dynamic-linking constraints that
   complicate static builds, especially on iOS / WASM ports
   later.

### Why not Qt 6, even though it has the best chrome

Three things drove this off the top of the list:

- **Two renderers fighting for the surface.** Qt's QRhi /
  QSGRenderer is its own GPU pipeline. Mixing it with our
  Vulkan stack means either we render under Qt (losing direct
  control) or we render into a `QQuickFramebufferObject`
  texture (adds latency + a frame of synchronization).
- **Two event loops fighting for input.** Qt's event loop owns
  the message pump. Our hook system + GLFW input is what
  carries pen pressure / tilt through `WM_POINTER` (ADR 0017).
  Marrying both is doable but it's plumbing tax we'd pay
  forever.
- **Build weight.** Qt's FetchContent build is several minutes
  cold; our current full build is ~30 seconds. The CI matrix
  multiplier (4 OS/config combos) makes that tax visible.

Qt remains the obvious **v1.0 chrome candidate** — its widget
polish is what users will eventually expect. The migration
strategy is in the phase boundary section.

### Why not Slint, even though it's modern

Slint is the most interesting future contender — declarative,
Vulkan-aware, MIT-licensable, smaller than Qt. Ruled out for
**v0.x** because:

- Smaller ecosystem: fewer reference apps, fewer Stack Overflow
  questions, fewer "this is how to ship X widget" precedents.
- DSL learning curve: `.slint` files are a separate compile
  step. Worth it for a settled UI; expensive at the prototyping
  stage when every panel is in flux.
- Limited text-editing primitives — the notes side needs rich
  text widgets that Slint's stdlib doesn't yet ship.

Re-evaluate at the v1.0 migration point. Slint vs. Qt will be a
real fight then.

### Why not webview / Tauri

Webview UI for a low-latency ink-stylus product is a category
error. Every pen sample crosses an IPC boundary; the renderer is
a browser engine we don't control; the memory baseline is
~200 MB. Out of consideration.

### Why not custom IMGUI

Building our own immediate-mode UI on top of Vulkan from scratch
is **what we'd do in year three**, not month one. Dear ImGui's
50k lines of MIT-licensed code is six months of work donated to
us; reinventing it on day one would push the actual product
features back by a quarter for zero user-visible benefit.

If Dear ImGui's limitations eventually constrain the product,
we have two cheaper escape hatches: (a) build custom widgets
inside Dear ImGui (it's designed for this — `ImDrawList` exposes
raw drawing), or (b) replace the chrome with Qt/Slint while
keeping the canvas in our own renderer (the phase boundary).

### Module boundary (already in place)

The existing `ui/` module's 4-area split is the API contract
that makes the v1.0 swap feasible:

```
ui/
  view/      — observes domain state, emits draw calls
  widget/    — composite primitives (LayerPanel, OutlineTree, …)
  theme/     — colors, fonts, spacing
  binding/   — hook-channel → view-state plumbing
```

**No domain or engine code depends on `ui/`.** Domain types
(`Document`, `Selection`, `UndoStack`) are pure; engine types
(`LayerCompositor`, `SelectionMask`) are pure GPU. The UI
subscribes to domain hook channels and renders. When v1.0
arrives and we swap chrome, `view/` and `widget/` get
re-implemented against the new toolkit; `theme/` and `binding/`
get adapted; `domain/` + `engine/` are unchanged. That is the
contract; this ADR pins it.

### Phase boundary — when to revisit

Trigger a re-evaluation when **any** of these become true:

1. **Theming demands exceed ImGui's flex.** If we find ourselves
   reimplementing button rendering / focus rings / IME
   composition popups from scratch inside ImGui, we're paying
   custom-IMGUI cost without the freedom.
2. **Korean / Japanese / Chinese IME support breaks.** ImGui's
   IME story is improving but not native. If user testing shows
   composition friction for our primary user base, we need a
   toolkit with first-class IME (Qt, Slint, native).
3. **Accessibility audit needed.** Screen readers + keyboard
   navigation + high-contrast modes. ImGui's accessibility is
   limited; a public-release product needs a toolkit that
   exposes ARIA-equivalent metadata to the OS accessibility
   tree.
4. **Public release / paying users.** Then the "looks like a
   debug tool" risk is real. Either a heavy theming pass on
   ImGui or a chrome migration to Qt/Slint.

The first three are concrete signals. The fourth is the
launch-readiness review where we look at user studies and
either commit to ImGui-with-deep-theming or migrate. ADR 0027b
will record that decision.

### Specific commitments for v0.x

- Use **Dear ImGui docking branch** (window docking, multi-viewport
  off by default — adds platform complications we don't need).
- Pin to a tagged release (latest at write time: `v1.91.5` or
  newer).
- Use the official `imgui_impl_vulkan` + `imgui_impl_glfw`
  backends — no fork, no patches we maintain.
- Wire `LayerCompositor` into the frame: ImGui draws first into
  its own command buffer pass, then the compositor renders
  layers into the canvas, then a final pass composes both onto
  the swapchain image. (Alternative: compositor first, then
  ImGui as an overlay. Decide in the scaffold PR.)
- The font atlas is built at startup from a custom font with a
  CJK glyph range bundled — committed in the scaffold PR.
- Each ImGui window has a stable string ID so `ini` state
  (panel positions, sash splits) round-trips across sessions.

## Alternatives considered

(Covered in the comparison table + per-stack rationale above:
Qt 6, Slint, Tauri/webview, custom IMGUI, no-UI / CLI-only.)

**No-UI / CLI-only** is the dark-horse option we briefly took
seriously: drive the whole engine from `.noted` files + a CLI,
ship the visible UI in v1.0. Rejected because the product needs
**ink-stylus visual feedback within tens of milliseconds**, which
requires a visible canvas, which requires *some* UI scaffold.
Even if every menu was a hotkey, the canvas + ink overlay is
the minimum viable UI surface.

## Consequences

- New dep ahead (separate PR, the **scaffold** PR): Dear ImGui
  docking branch via FetchContent SYSTEM. Two TUs:
  `imgui_impl_vulkan.cpp` + `imgui_impl_glfw.cpp` plus the core
  `imgui.cpp`. ~50k lines C++, MIT.
- The existing 4-area `ui/` module structure stays. Empty
  stubs get filled in via the scaffold + first concrete widget
  PRs.
- `app/src/main.cpp` rewrites away from the textured-quad demo
  in the scaffold PR — replaces with: GLFW window → Vulkan
  init → ImGui init → frame loop calling `LayerCompositor` +
  ImGui.
- Build weight grows modestly: Dear ImGui adds ~5 seconds to a
  cold full build. CI matrix tax: 4 × 5s = 20s. Acceptable.
- License stack: GLFW (zlib/libpng), VMA (MIT), stb (MIT/PD),
  nlohmann/json (MIT), miniz (PD), gtest (BSD-3), tracy (BSD-3),
  Dear ImGui (MIT). Adding ImGui changes nothing about the
  permissive baseline.
- The `ui/` module's docstring is updated to name Dear ImGui
  as the v0.x implementation while preserving the swap-path
  contract.
- No code changes ship in **this** ADR PR — the actual scaffold
  follows. This PR is the design commitment + the docstring
  refresh + the HANDOFF roadmap update.

## Follow-ups (next several PRs)

1. **`feat/ui-imgui-scaffold`** — FetchContent ImGui, init/shutdown,
   first ImGui frame inside the existing window, `main.cpp`
   rewrite to drop textured-quad demo. Replaces ADR 0017's
   "what does NOT work yet: no UI chrome" gap. ~4-6 h.
2. **`feat/ui-compositor-wire`** — `LayerCompositor::composite`
   called inside the ImGui frame loop. First time the user can
   see a layer graph rendered with real blend modes + a real
   selection mask. ~3 h.
3. **`feat/ui-debug-overlay`** — Tracy-style overlay: FPS,
   harness counters, fallback counts. Validates the
   `binding/` channel → view plumbing on a low-stakes target
   before the real document UI lands. ~2 h.
4. **`feat/ui-document-shell`** — Window with menu bar, layer
   panel, outline tree (Document.preorder visualization),
   undo/redo buttons backed by UndoStack. The first end-to-end
   product-shaped surface. ~8 h.
5. **`feat/ui-theme-pass`** — Custom ImGuiStyle, font atlas with
   CJK ranges, dark/light theme switching. Pushes back the
   "looks like debug tool" risk. ~4 h.
6. **(deferred to v1.0 review)** — `feat/ui-stack-revisit`. Decide
   Qt / Slint migration vs. ImGui-with-deep-theming based on
   user testing.
