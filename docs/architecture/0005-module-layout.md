# ADR 0005: Module layout — layered C++ libraries

**Status:** Accepted
**Date:** 2026-05-15

## Context

A repo that mixes platform code, domain logic, GPU code, and UI in one library
becomes a giant compile-everything-on-every-change ball. We want clear
dependency direction, fast incremental builds, and the ability to swap any
single layer without touching the others.

## Decision

Six top-level C++ libraries, strict dependency direction (arrows point in the
direction of `target_link_libraries`):

```
            app
             │
   ┌─────────┼─────────┐
  ui      plugin    platform
   │         │         │
   └───────domain──────┘
             │
           engine
```

- **engine** — GPU abstraction, tile store, color, jobs, memory, hook system,
  error primitives, harness. No knowledge of documents.
- **domain** — pure logic: document tree, layer DAG, commands, CRDT. Depends
  only on `engine` for error and hook types.
- **platform** — per-OS windowing/input/IO. Depends on `engine`.
- **plugin** — WASM host. Depends on `engine`.
- **ui** — view layer. Depends on `domain` and `platform`.
- **app** — entry point. Depends on everything; contains `main`.

Each library follows the same layout:

```
<module>/
  CMakeLists.txt
  include/noted/<module>/    # public headers (consumed by other modules)
    <module>.hpp             # umbrella header
    <subarea>/...            # grouped public headers
  src/                       # implementation; never include each other's src/
  tests/                     # module-local unit tests (also runs via /tests)
```

The CMake helper `noted_add_module(NAME ... SOURCES ... PUBLIC_DEPS ...)`
enforces the layout: include path is `include/noted/<module>` only, warnings
and hardening flags are applied uniformly.

## Alternatives considered

- **Single library.** Quick to start, but full-rebuild pain accumulates and
  forces dependency cycles.
- **Header-only everywhere.** Wrecks compile times once the codebase grows.
- **One module per feature.** Too granular; the linker graph becomes the
  documentation. Six layers is the sweet spot.

## Consequences

- Cyclic dependency between modules is a build error, not a warning. Good.
- Touching a leaf module (e.g. `ui/widget/`) only rebuilds itself and `app`.
- New modules go through the same helper, keeping the build surface uniform.
