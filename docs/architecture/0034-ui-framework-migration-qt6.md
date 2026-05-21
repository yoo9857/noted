# ADR 0034 — UI framework migration to Qt 6 + QML

**Status:** Accepted (design). Implementation phases across multiple
follow-up PRs.
**Date:** 2026-05-21
**Supersedes:** Phase B of [ADR 0027 (UI stack selection)](0027-ui-stack-selection.md) —
the v0.x "Dear ImGui product shell" is the previous chapter; this
ADR is its v1.0 successor.

## Context

ADR 0027 picked Dear ImGui as the v0.x product shell with the
explicit understanding that it was a **placeholder** for native UI
once the engine stabilised. Ten months of feature work later the
engine + domain + persistence are solid (508 tests, four merged
slices of feature + fix work in the past day alone), so the
"v1.0 boundary" the ADR talked about has arrived.

The triggering UX feedback (2026-05-21):

> 우리가 UI/UX를 전면개편해야해, 너무 딱딱한 것 말고, 버튼형식 아이콘들이
> 12시 방향에, 맥 소프트웨어 UI/UX처럼 업그레이드 개선되어야함
> 와이어프레임, 레이아웃등 완벽하게
> 전문적으로
> 20년 개발자의 기술스택으로 완벽하게

Translation: the user wants a Mac-grade product UI — top-centre icon
toolbar, polished chrome, contextual panels, professional layout —
delivered with a senior-grade stack. ImGui in its current form gets
us "functional debug overlay"; the leap to "Photoshop / Procreate /
Goodnotes 7 grade product" requires a real native UI framework.

### Why not stay with ImGui

- ImGui's visual model is **immediate-mode + cell-based** — every
  control sits on a 1-px grid of rectangles. Rounded chrome,
  blur-effect translucency, vibrant animation, traffic-light
  window controls, and the system-typography expectations of a
  Mac-grade app are all swimming against the framework's grain.
- Theme customisation goes a long way — we've already pushed it
  hard with dark-grey desk + page shadows + AA edges — but every
  pixel of polish needs hand-tuned ImGuiStyle config, and the
  ceiling is "looks intentional in an ImGui app" not "looks at
  home on macOS".
- Per-platform integration (native menu bar on macOS, Mission
  Control behaviour, macOS keyboard chords, accessibility) is
  effectively out of reach with ImGui — its event model isn't a
  participant in the platform's input pipeline.
- Multi-document tabs, dockable panels with system-grade behaviour
  (snap, swap, tear-off), keyboard navigation that reads as
  native — all areas where ImGui is functional but the seams show.

### Why Qt 6 + QML over the alternatives

| Option | Verdict |
|---|---|
| **Qt 6 + QML** | Industry standard for desktop pro-apps with custom rendering (Maya, Houdini, Krita, MuseScore, OBS Studio). 25-year track record. Mature Vulkan integration via `QVulkanInstance` + `QWindow` / `QWidget`. QML provides declarative Mac-style chrome with built-in animations. LGPLv3 + commercial. Crosss-platform with native look per OS. |
| **Slint** | Newer (2020), C++ bindings clean, but desktop pro-app track record is thin. Fluent SDF rendering. Reasonable second choice if Qt's footprint becomes a problem; revisit if Slint hits a 1.x desktop-stable milestone. |
| **Native split (Cocoa + Win32 + GTK)** | Most authentic look per platform. Procreate-grade UX but **3× the UI codebase + 3× the maintenance** for a solo-dev project. Rejected — the cost-benefit doesn't work below a 10-developer team. |
| **Tauri / Electron / web stack** | Not viable: we need native Vulkan rendering, GPU memory ownership, deterministic frame timing. Web stacks fight all three. |
| **Custom UI in Vulkan** | A research project, not a product. Re-implements 25 years of UI toolkit work badly. Rejected. |
| **Dear ImGui (current)** | The starting point we're migrating away from. Stays only for transitional canvas overlays during the migration window. |

### Senior-grade stack reference

For "Photoshop-surpassing notes + raster editor" the established
peer stack is:

- **Krita** — Qt + KDE Frameworks, Vulkan via custom renderer.
  Closest analogue to our goal; ships professional raster +
  vector + sketching UI on Qt.
- **MuseScore 4** — Qt 6 + QML, custom scene-graph rendering.
  Recently migrated FROM Qt Widgets TO QML, with Mac-grade
  results.
- **OBS Studio** — Qt 6 + custom rendering. Closest peer for the
  "Vulkan window embedded in a Qt shell" pattern we need.

We're standing on the shoulders of a well-known migration path,
not blazing trail.

## Decision

Migrate the product shell to **Qt 6.7+** with **QML** (Quick) for
the chrome layer. The Vulkan rendering pipeline (engine + canvas +
compositor + stroke engine + page renderer + all shaders) is
**unchanged** — it stays exactly as it is today. The QML layer
becomes the new host for the Vulkan surface and replaces ImGui as
the source of menus / toolbars / panels / dialogs.

### Subsystem boundary

```
┌─────────────────────────────────────────────────────────────┐
│  Qt 6 + QML Application                                     │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  Chrome layer (QML / .qml files)                       │  │
│  │   - top floating toolbar (icon buttons)                │  │
│  │   - menu bar (native on macOS)                         │  │
│  │   - right contextual panel                             │  │
│  │   - status bar                                         │  │
│  │   - dialogs, file pickers, tooltips                    │  │
│  └─────────────────┬────────────────────────────────────┘  │
│                    │ signals + properties                   │
│  ┌─────────────────▼────────────────────────────────────┐  │
│  │  C++ controller layer (existing app/ extended)        │  │
│  │   - DocumentSession, ToolState, BrushOptions exposed  │  │
│  │     to QML via Q_PROPERTY / signals                   │  │
│  │   - Camera, StrokeEngine, ShapeRecognizer unchanged   │  │
│  └─────────────────┬────────────────────────────────────┘  │
│                    │                                        │
│  ┌─────────────────▼────────────────────────────────────┐  │
│  │  Canvas viewport widget (QWidget / QQuickItem)         │  │
│  │   - Owns the VkSurfaceKHR via QVulkanInstance         │  │
│  │   - Hosts our existing 4-pass canvas pipeline         │  │
│  │   - Pointer events arrive via QMouseEvent /           │  │
│  │     QTabletEvent and feed our hook registry           │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

Everything **below** the chrome layer is preserved bit-for-bit.
The migration's scope is **the chrome only**.

### Phased migration (per-PR plan)

Each phase produces a working build with a green CI run. **No
phase leaves the product unusable.**

| # | Phase | Scope | Surface area |
|---|---|---|---|
| 0 | Qt build integration | vcpkg/FetchContent Qt 6.7; CI Linux + Windows verify Qt build | CMake changes, no runtime change |
| 1 | Shell migration | Replace GLFW with `QGuiApplication` + `QWindow` hosting Vulkan. ImGui still renders all UI inside the Vulkan canvas. Window opens, canvas renders, input flows. | ~600 LOC swap; no UX change yet |
| 2 | Theme + chrome | QML window with rounded corners, drop shadow, custom title bar (Mac traffic lights / Windows custom). Light + Dark variants matching system. | ~400 LOC QML + theme code |
| 3 | Top-centre toolbar | QML floating toolbar with SVG icon buttons (Lucide icon set). Tool selection drives ToolState via Q_PROPERTY. ImGui's `tool_palette` widget retired. | ~500 LOC QML + 200 LOC C++ bindings |
| 4 | Right contextual panel | Single right-side panel whose content reflects the active tool (brush options / shape options / layer panel / outline panel as QML components). ImGui's per-tool panels retired. | ~800 LOC QML + 300 LOC C++ services |
| 5 | Menu + status bar | Native macOS menu bar via Qt; minimal Windows menu; QML status bar at bottom with monospace metadata. ImGui `menu_bar` widget retired. | ~300 LOC QML + 200 LOC C++ |
| 6 | Dialogs + file pickers | QFileDialog replaces nativefiledialog-extended; QML dirty-prompt modal; tooltips via Qt. nfd dependency removed from CMake. | ~200 LOC QML + 200 LOC C++ |
| 7 | Tear-out | ImGui dependency + every `noted::ui::widget::*` ImGui call removed. Build shrinks. | ~−2000 LOC net |

**Total estimate: 8 PRs, 2-3 weeks at the cadence of the recent
slices.**

### Risk mitigation

- **Phase 1 is the only "all-or-nothing" change.** Everything
  after is incremental and reversible — if phase 4 introduces a
  regression we revert that PR alone, not the whole stack.
- **Test surface unchanged.** Domain / engine / app tests
  exercise pure logic and Vulkan recording; they don't touch
  ImGui or Qt. They keep passing through the migration.
- **Vulkan validation stays at zero errors.** Phase 1's job is
  proving QWindow's Vulkan integration matches what GLFW
  provided — same `VkSurfaceKHR`, same `QueueFamilyProperties`,
  same swapchain semantics. We verify by running the existing
  smoke test against the Qt shell.
- **CI cost.** Adding Qt to the build matrix adds ~3-5 min per
  CI run for Qt fetch + compile. Mitigated by `ccache` and Qt
  build artefact caching across CI runs.

### Out of scope

- **Canvas rendering changes.** Strokes / pages / shapes / texts
  / images / SDF AA / per-segment capsule — **none of this
  changes**. The Vulkan pipeline is untouched.
- **Touch / pen pressure / tilt.** Qt 6 has `QTabletEvent` and
  `QPointerDevice`; our existing pressure infrastructure flows
  through unchanged. Tilt-aware brush rendering is a separate
  follow-up that the migration neither helps nor hinders.
- **Native macOS-only features** (continuity camera, hand-off,
  iCloud sync). Out of v1.0; revisit when there's a Mac user
  base.

## Consequences

- The product looks and feels like a native Mac app at v1.0,
  with the same accessibility / input / chrome / animation
  characteristics users expect from professional desktop
  software in 2026.
- The codebase carries Qt 6 as a major dependency. Acceptable
  given Qt's lifespan and our pro-app peer group.
- ImGui can be re-introduced for **debug-only overlays** (Tracy
  HUD, hot-state introspection in dev builds) by gating it
  behind a `-DNOTED_DEV_TOOLS=ON` CMake option. Out of scope
  for the migration itself; entered as a follow-up if it turns
  out we miss the in-app dev overlay surface.
- The migration is sequential — every phase needs the previous
  to be green before starting. Multi-session, multi-PR effort,
  tracked in HANDOFF.md's "Phase F — UI v1.0 migration".

## Alternatives considered

- **Heavy ImGui theming + Lucide icon font** — fastest but
  produces "tasteful ImGui app", not "Mac app". Rejected
  per user feedback.
- **Slint** — promising but desktop track record is thin. Park
  the option for re-evaluation at the 1-year mark of Qt
  migration if Qt's footprint becomes painful.
- **GTK4 / libadwaita** — strong Linux story, no real Mac
  support. Rejected.
- **Flutter desktop** — relies on Skia for rendering; integrating
  our Vulkan canvas means duplicate render pipelines + complex
  buffer sharing. Rejected.
- **Hybrid (native chrome + Vulkan canvas)** — what we're
  actually doing. Confirmed best path.

## Implementation order — first deliverables

Subsequent PRs after this ADR:

1. **PR A — Build integration**: Qt 6 fetched via CMake
   FetchContent or vcpkg manifest, no runtime change. CI must
   pass with Qt available.
2. **PR B — Shell swap**: GLFW → QGuiApplication + QWindow.
   ImGui still inside. Window opens, canvas renders, inputs
   flow.
3. (then phases 2-7 in order.)
