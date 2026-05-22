# ADR 0035 — `.noted` v9 schema: pen-dynamics persistence

**Status:** Accepted.
**Date:** 2026-05-23
**Builds on:** [ADR 0025 (`.noted` JSON format)](0025-document-json-format.md),
[ADR 0029 (vector-ink polyline)](0029-vector-ink-polyline.md),
[ADR 0026 (`.noted` archive container)](0026-noted-archive-container.md).

## Context

Sessions 2026-05-22 shipped the full pen-dynamics stack: a 2-handle
bezier `PressureCurve` replacing the single `alpha_gamma` scalar
(PR #119), real soft-edge SDF brush (PR #118), velocity-aware width
("speed taper", PR #120), and tilt-aware calligraphy width (PR #121).
The tessellator now reads three additional inputs that the file
format did not yet persist:

- per-`StrokeSample`: `t` (seconds since stroke start), `tilt_x`,
  `tilt_y` (pen tilt unit vector projected onto the canvas plane);
- per-`BrushStyle`: `PressureCurve pressure_curve` (replaces
  `alpha_gamma` as the authoritative pressure→alpha mapping),
  `velocity_blend ∈ [0, 1]`, `tilt_blend ∈ [0, 1]`.

The v8 schema only persists `(x, y, pressure)` triples + the legacy
`alpha_gamma` scalar. Saving and re-loading a v8 file under the v8
reader silently discards every dynamics value the user authored —
the file round-trips at the data level but the rendered pen *feel*
changes. Senior bar: persistence must be lossless against the model
the engine actually uses.

## Decision

Bump the on-disk schema to **v9** and persist all three inputs.

### Sample-array layout

v7/v8 used a stride-3 flat float array; v9 widens to stride-6:

```
v7/v8 :  [x0, y0, p0,           x1, y1, p1,           ...]   // 3 floats per sample
v9    :  [x0, y0, p0, t0, tx0, ty0, x1, y1, p1, t1, tx1, ty1, ...]   // 6 floats per sample
```

The flat layout is preserved (vs. an array-of-objects) for the same
reason as v7: named-key overhead would inflate a 500-sample stroke
by ~4× on disk. Stride is selected by the file's `version` field at
parse time — no in-file discriminator. A 500-sample v9 stroke costs
~3 000 numbers; well inside the JSON parse + zip budget.

### Style-block additions

Three additive keys on the per-stroke `style` object:

```jsonc
"pc": { "h1x": <float>, "h1y": <float>,    // PressureCurve handles, [0, 1]
        "h2x": <float>, "h2y": <float> },
"vb": <float>,                              // velocity_blend, [0, 1]
"tb": <float>                               // tilt_blend, [0, 1]
```

All three are **optional on read** at every version. Strict-mode key
checking is widened to accept them on v7/v8 too (the writer never
emits them there, but a hand-crafted file supplying them with a
legacy version is loaded via the same migration path as v9-with-
neutral-defaults — no silent data loss).

### Forward compatibility — v7/v8 → v9 reader

When a v7/v8 file lands in a v9 reader:

| Field | Migration |
|---|---|
| `samples` stride | 3 (legacy); the inner loop reads `(x, y, p)` and leaves `t = tilt_x = tilt_y = 0` at the `StrokeSample` defaults. Collapses to pre-velocity, pre-tilt rendering bit-for-bit. |
| `style.pc` | Absent → `PressureCurve::from_gamma(alpha_gamma)`. The closed-form bezier reproduces `pow(x, gamma)` on the diagonal samples (1/3, 2/3), so the user-facing pen feel survives the upgrade. |
| `style.vb`, `style.tb` | Absent → 0 (no velocity / tilt damping). |

Idempotence: a v8 file → v9 reader → v9 writer round-trip stamps the
migrated curve into the file. A subsequent v9 reader reads exactly
the curve `from_gamma(ag)` produced, so re-saving never drifts.

### Backward compatibility — v9 file in v8 reader

Hard fail: v8 reader rejects `version: 9` at the top-level check
("supported [1, 8]"). This is the existing contract — silently
truncating unknown fields would risk data loss on round-trip
(ADR 0025). Users on stale binaries get an actionable error
("unsupported version") rather than a quietly-corrupted file.

### Clamping at the data boundary

Every new numeric input is clamped at load time so a corrupted /
hostile file cannot smuggle a degenerate value past the renderer:

- `pc.h{1,2}{x,y}` clamped to `[0, 1]` (matches `PressureCurve`'s
  unit-square invariant the tessellator's alpha shader assumes).
- `vb`, `tb` clamped to `[0, 1]`.
- Per-sample `t / tilt_x / tilt_y` are not clamped — they're free
  inputs and the tessellator already handles out-of-band values
  defensively (large dt → velocity cap; tilt magnitude clamped per
  segment).

## Consequences

**Positive:**

- Pen-dynamics behaviour survives save / load — the headline
  feature isn't quietly lossy.
- Existing v7/v8 files load with the same rendered output they had
  on the legacy reader (the migration is shaped to match the legacy
  behaviour exactly, not a "best guess").
- Future curve evolutions (e.g. a 4-handle bezier) cleanly land in
  the v9 `pc` slot since strict-mode catches unknown keys early —
  we'll get a noisy parser failure on a typo, not silent loss.

**Negative:**

- Stroke samples cost 2× the bytes on disk. At our document scales
  (typical document < 5 MB, observed max ~12 MB) this is a wash;
  the zip container compresses the new 0-valued columns aggressively
  on legacy-style mouse input.
- A new version means yet another back-compat path in the loader.
  The migration table above and the strict-key tests pin the
  contract so future drift gets caught at PR time.

**Neutral:**

- `alpha_gamma` (`ag`) stays in the schema. It's no longer the
  rendering authority (the tessellator reads `pressure_curve`), but
  removing it would break the v7/v8 → v9 migration (we still need
  `ag` to synthesise `pc`) and the UI's "Pressure" slider still
  drives the curve via `from_gamma` at the source. Senior call:
  keep the legacy field, don't grow a "pc is the truth" footnote.

## Alternatives considered

1. **Stride-6 only for samples that need it (sparse encoding).**
   Rejected — adds a discriminator field per sample and complicates
   the inner loop. The dense layout is simpler and the bytes are
   cheap.
2. **Separate `samples_v9` field, keep `samples` legacy.**
   Rejected — strict mode would have to know about both keys, and
   it's bookkeeping for no win: the version field already tells the
   parser the shape.
3. **Embed `pc` as `[h1x, h1y, h2x, h2y]` (flat array).**
   Rejected for readability — the curve editor in the UI labels
   handles by name, and diffs of `.noted` JSON read more cleanly
   with named keys. Disk cost is 4 floats either way.
