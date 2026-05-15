# ADR 0018: Pressure-driven stamps in the stroke engine

**Status:** Accepted
**Date:** 2026-05-16

## Context

ADR 0015 shipped the stroke MVP with a constant 4 px black tip and
`alpha = default_alpha * pressure`. With `feat/pen-input` (ADR 0017)
landing alongside, `e.pressure` now carries real digitizer values
on Windows. Time to make the brush *feel* like a brush.

Three things have to land together:

1. **Per-engine brush parameters** — a struct the host can configure
   (debug overlay, brush presets, future UI). Hard-coded constants
   in `stroke_engine.cpp` were fine for the MVP demo; they aren't fine
   the moment we want two brushes.
2. **A pressure curve** — linear isn't right for either radius or
   alpha. Linear radius is OK (Photoshop default). Linear alpha makes
   light touches feel too heavy; a gamma curve matches the
   perception literature and what every real painting app ships.
3. **A pure mapping function** — testable on every host with no
   digitizer, no GPU, and no event loop. The stroke engine's
   accumulation code calls it; unit tests call it directly.

## Decision

### `BrushStyle` struct + `stamp_from_pressure()` pure function

```cpp
struct BrushStyle {
    float min_radius_px  {2.0F};
    float max_radius_px  {10.0F};
    float softness_ratio {0.20F};  // fraction of radius
    float alpha_gamma    {1.8F};
    float r, g, b, a     {0.0F, 0.0F, 0.0F, 1.0F};
};

[[nodiscard]] auto stamp_from_pressure(
    const BrushStyle& style, float pressure) noexcept -> Stamp;
```

`stamp_from_pressure` is pure, header-declared, source-defined. It:

- Clamps `pressure` to `[0, 1]` (also handles NaN → 0).
- Lerps radius linearly: `r = min + (max - min) * p`.
- Computes alpha as `style.a * pow(p, alpha_gamma)`. The gamma
  defaults to 1.8 — the same exponent the Krita / Photoshop default
  pressure curves expose for ink. `alpha_gamma <= 0` is treated as
  linear so the function is safe under any caller-supplied junk.
- Derives softness as `max(1px, radius * softness_ratio)`. The 1 px
  floor keeps tiny stamps anti-aliased; the radius-proportional band
  keeps anti-aliasing constant in *visual* weight as the stamp scales.
- Color (`r`, `g`, `b`) passes through unchanged. Only alpha responds
  to pressure.

### Engine integration

`StrokeEngineCreateInfo` gains a `BrushStyle brush{}` field. The
factory captures it into the engine. `on_pressed` and `on_moved` now
call `stamp_from_pressure(brush_, e.pressure)` and stamp the result
at the event's `(x, y)`. The hard-coded constants are gone.

`StrokeEngine::brush()` / `set_brush(b)` expose live tuning. Mutation
affects future stamps only — past stamps keep the style they were
drawn with. This is the right default for an in-app brush slider
(retroactively re-shaping ink would be confusing) and the right
default for [[layer-compositor]] integration (each stamp records its
own style, the compositor doesn't have to re-derive).

### Why parameters and not a template

Brushes are a runtime concept. Users open a brush picker, drag a
slider, switch presets. A template would force one `StrokeEngine`
type per brush — wrong shape entirely.

### Default values

- `min_radius = 2`, `max_radius = 10` — visible at all pressures on a
  1080p monitor, doesn't run away at full press.
- `softness_ratio = 0.20` — perceptually soft edge without losing
  shape definition.
- `alpha_gamma = 1.8` — light touches feel light, harder strokes
  still saturate quickly. Matches what's in Krita's "Basic Smooth"
  default.
- Color = opaque black on white canvas. The note-taking demo's
  contrast.

## Alternatives considered

- **Apply pressure to alpha only.** What ADR 0015 did. Real ink
  scales width with pressure too; users notice immediately when it
  doesn't.
- **Bezier curve per parameter** (radius curve, alpha curve, ...).
  Eventually yes — the brush UI ships with curve editors. For MVP
  it's slider-equivalent UX with one less degree of control.
- **Cubic spacing / catmull-rom path smoothing.** Right answer for
  "natural ink" — without it, fast strokes have visible discrete
  stamps. Out of scope here; lands as `feat/stroke-smoothing` once
  `feat/layer-compositor` (P3 #8) is in.
- **Per-stamp color randomness** for paint-feel. Cheap and lovely
  but UX-load (artists configure or disable it). Park.
- **Templates on BrushStyle.** Rejected — see above.

## Consequences

- The app now responds to real pressure on Windows (via ADR 0017's
  pen subclass). Mouse users see `pressure = 1.0F` and get the
  full-pressure stamp — same as before, sensible default.
- `noted::stroke` gains 8 new unit tests covering the pure mapping:
  linear radius lerp, gamma alpha curve, softness floor + scaling,
  pressure clamping (incl. NaN), color pass-through, the on-press
  brush snapshot, and `set_brush()` mutation.
- The stamp pipeline itself doesn't change. Same push-constant
  layout, same shader. All the new behavior lives in the C++ math
  fed into the existing push-constant struct.
- Future per-stroke styling work plugs into `BrushStyle` — e.g.,
  pressure-driven hue, texture maps, jitter. Each is a struct field
  + a few lines in `stamp_from_pressure`.

## Follow-ups

- **Path smoothing** — Catmull-Rom interpolation between event
  samples, plus minimum-spacing dedup. Brings the look from "discrete
  disks" to "natural ink" on fast pen strokes.
- **Brush presets** — a `BrushLibrary` of named `BrushStyle` values,
  hot-swappable via the future debug UI.
- **Pen tilt → ellipse / chisel shape** — pairs with future
  oriented-stamp shaders.
- **Pressure-driven hue / opacity / size curves** as user-editable
  Bezier curves once the UI stack lands.
- **Eraser tip** — `BrushStyle` with `blend = subtract` or alpha-only
  destination factor once `feat/layer-compositor` exposes blend modes
  in the stamp pipeline.
