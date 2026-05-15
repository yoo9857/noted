# ADR 0016: Layer graph — the Photoshop side's data backbone

**Status:** Accepted
**Date:** 2026-05-16

## Context

The Goodnotes side has a working pipeline now (canvas + stroke MVP).
The Photoshop side has had a 16-line stub for `domain::Layer` since
day one. The compositor (P3 #8) and every downstream Photoshop feature
needs a real layer data model to build against. This ADR specifies it.

Three concerns to settle up-front:

1. **What goes in the domain layer vs the GPU layer?** Pixels are GPU.
   Strokes are GPU buffers. Layer topology, blend mode, opacity,
   visibility, names — those are pure data and have to round-trip
   through the file format and the undo stack. So: the domain model is
   topology + parameters; the GPU side keeps payload (image handles,
   stroke collections) and resolves them on demand.

2. **DAG vs tree vs flat list?** Photoshop layers are flat lists with
   "clipping mask" being a special-case "this layer modifies the one
   below it" relationship. Modern editors (Affinity Photo, Procreate)
   prefer a DAG so adjustment layers, masks, and groups all fall out
   of "layer N takes M inputs and produces one output". DAG matches
   non-destructive editing without special cases.

3. **How does the API handle bad states?** Cycles, dangling
   references, NaN opacities. Result<T> is the project default; the
   question is whether to mutate-then-validate or validate-then-mutate.
   The latter is what we want — failed operations leave the graph
   exactly as it was.

## Decision

### Type model

```cpp
using LayerId = std::uint64_t;
inline constexpr LayerId invalid_layer_id = 0;

enum class BlendMode : std::uint8_t { /* 16 standard modes */ };
enum class LayerKind : std::uint8_t {
    bitmap, stroke, adjustment, group, mask,
};

struct LayerNode {
    LayerId               id;
    LayerKind             kind;
    BlendMode             blend;
    float                 opacity;  // [0, 1]
    bool                  visible;
    std::string           name;
    std::vector<LayerId>  inputs;
};

class LayerGraph { /* ... */ };
```

`LayerNode` holds **topology + parameters**, nothing else. Payload
data (bitmap pixels, stroke geometry) is the GPU layer's
responsibility — the compositor maps `LayerId` to whatever resource
the kind requires.

Enum values for `BlendMode` and `LayerKind` are fixed (`= 0, 1, 2…`)
because they will end up in the `.noted` file format. New entries
append; existing entries never reorder.

### Identity scheme

`LayerId` is `std::uint64_t`, monotonically allocated, never reused
within a graph's lifetime. `0` is `invalid_layer_id`. This matches the
hook system's `Token` design — same trade-off, same rationale.

The graph stores nodes in `std::unordered_map<LayerId, LayerNode>`.
O(1) lookup; iteration order is insertion-undefined (callers go
through `topological_order()` when they need dependency-respecting
iteration). A vector + ID-to-index map would be denser in cache but
complicates remove_layer; the map wins for the next few PRs.

### Mutation contract — validate-then-mutate

Every mutating method returns `Result<void>`. On failure:

- The graph is left **exactly** as it was before the call.
- The Error explains *why* in human-readable text, with the offending
  ID(s) in the message.

`set_inputs` is the interesting one. The implementation
tentatively swaps the inputs list, runs cycle detection, and rolls
back if it fails. Two reasons over copy-the-whole-graph:

1. The graph can grow large (hundreds-to-thousands of layers in a
   real document); cloning per-mutation is a real cost.
2. The rollback path is one assignment; the failure path is rare;
   the success path stays hot.

### Cycle detection — iterative DFS with three-color marks

Standard graph coloring (white / gray / black). Iterative — explicit
stack — so deep DAGs don't blow the C++ stack. Returns on the first
back-edge.

The same DFS implementation is used by `validate()` and by the
internal cycle check in `set_inputs`. They diverge only in what they
report.

### Removal — refuse to drop referenced nodes

`remove_layer(id)` rejects with `invalid_state` when any other node
lists `id` in its `inputs`. The caller decides:

- "Rewire dependents first" (the safe Photoshop default) — call
  `set_inputs` on each dependent to drop `id` before remove.
- "Cascade delete" — caller composes the same primitive: walk
  dependents, rewire to bypass `id`, then remove.

The graph never silently drops references. Lost dependency edges
become silent rendering bugs; an explicit error is recoverable.

When `id == root_`, `remove_layer` clears the root to
`invalid_layer_id`. This is the one auto-fix, and it's safe because
"no root" is a documented well-formed state (= empty document).

### Opacity sanitization

`set_opacity(NaN)` becomes `0`. Out-of-range values clamp to `[0, 1]`.
The graph never stores a value it can't blend. Rationale: a caller
passing NaN is broken; turning the layer fully transparent is a safer
fallback than propagating NaN into the compositor.

## Alternatives considered

- **Tree of layers, not DAG.** Matches Photoshop's older UX exactly,
  but every adjustment / mask / clipping pattern becomes a special
  case. Rejected — modernity matters more than the Photoshop muscle
  memory.
- **Persistent (immutable) data structure** (e.g., copy-on-write).
  Pairs nicely with undo/redo but adds allocation pressure on every
  mutation. Defer to the Command-stack work (P4 #11) — it can wrap
  the mutable graph in versioned snapshots without changing the
  graph's API.
- **Variant-based LayerNode payload now.** Adds 30+ types' worth of
  weight before the compositor exists. Defer to per-kind PRs (one
  payload type per PR, hooked into kind=bitmap, kind=adjustment, …).
- **Indexed by `int` instead of monotonic `uint64_t`.** Allocation
  reuse breaks reference stability for hovered layers, selection
  state, etc. Monotonic IDs are the standard answer.
- **Throw on invalid state.** Conflicts with ADR 0003. Result<T>
  carries the same information without the exception machinery.

## Consequences

- The compositor (P3 #8) builds on a concrete `LayerGraph` — it
  receives a graph + a `topological_order()` and renders node by
  node. No more "we'll figure out the data model when the compositor
  needs it".
- `domain::Layer` stops being a stub. The module now has real tests
  (16) and pulls its weight in the dependency graph.
- The `.noted` file format (P4 #12) has a concrete schema target:
  serialize the node map and the root id, replay on load. BlendMode
  and LayerKind enum values are already wire-stable.
- The Command stack (P4 #11) wraps `LayerGraph` mutations in
  `LayerGraphCommand` derivatives. Each command captures the old
  state of the affected node(s) and uses `Result<void>` from the
  graph methods as its undo-precondition.
- Selection mask (P3 #9) becomes "another `LayerKind::mask` node
  with the gated layer as input". No new top-level concept needed.

## Follow-ups

- **Per-kind payload structs** — one PR per kind, wiring concrete
  data (bitmap image handle, stroke collection ID, adjustment params).
- **Group flattening** — the compositor will need to walk groups
  transparently or honor them as fold-points (UX decision pending).
- **LayerGraph in the Command stack** — `Command::apply()` /
  `Command::revert()` derive their pre/post state from the graph's
  `Result<void>` semantics.
- **CRDT support** for collaborative editing (existing CRDT module
  stub) — layer node ids and the input lists are the natural CRDT
  primitives.
