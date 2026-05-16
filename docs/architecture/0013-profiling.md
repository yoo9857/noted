# ADR 0013: Tracy for frame-grained profiling

**Status:** Accepted
**Date:** 2026-05-16

## Context

The harness has had `ScopedTimer` and `Counter` since ADR 0004. They publish
wall-clock spans and atomic counters via hook channels — useful for an
in-app debug overlay but invisible to an external profiler.

To work on real workloads — stroke rendering, layer compositing, large
import paths — we need a profiler that:

1. Captures **per-frame** CPU samples without measurable runtime overhead.
2. Lets us see **flame graphs** of where the frame is spent, with no
   re-instrumentation between debugging sessions.
3. **Connects from a remote machine** so a profiler GUI doesn't compete
   for the GPU we are debugging.
4. Is **safe in release builds** — toggleable at runtime, not just at
   compile time, so we can ship binaries with the profiler compiled in
   for production hot-fixes.
5. Adds **plots** of counter values over time, so `engine.frames.total`,
   image-upload bytes, etc. show up next to the CPU timeline.

This is exactly the profile that Tracy targets, and matches what the
HANDOFF roadmap (`feat/tracy-integration`) called out as P1 work.

## Decision

Adopt **Tracy 0.11.x** via `FetchContent`, gated by `NOTED_ENABLE_TRACY`
(default `OFF`).

### Build configuration

```
option(NOTED_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)
if(NOTED_ENABLE_TRACY)
    set(TRACY_ENABLE    ON  CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy GIT_REPOSITORY ... GIT_TAG v0.11.1 ...)
    FetchContent_MakeAvailable(tracy)
endif()
```

`TRACY_ON_DEMAND=ON` is critical: Tracy only starts collecting when a
profiler client connects. Otherwise a Tracy-enabled binary buffers
nothing and pays only the macro overhead. This is what lets us flip the
build flag on for release builds.

### Integration points

The engine never includes `tracy/Tracy.hpp` directly. Instead a single
shim header — `noted/engine/profile.hpp` — provides macros that expand to
Tracy when enabled and to no-ops otherwise:

| Macro                          | Tracy expansion        | When to use                          |
|--------------------------------|------------------------|--------------------------------------|
| `NOTED_PROFILE_ZONE()`         | `ZoneScoped`           | Function-scope sampling              |
| `NOTED_PROFILE_ZONE_N(lit)`    | `ZoneScopedN(lit)`     | Named sub-scope                      |
| `NOTED_PROFILE_FRAME()`        | `FrameMark`            | End-of-frame boundary                |
| `NOTED_PROFILE_FRAME_N(lit)`   | `FrameMarkNamed(lit)`  | Named frame loop (UI vs background)  |
| `NOTED_PROFILE_PLOT(lit, v)`   | `TracyPlot(lit, v)`    | Counter / value time series          |
| `NOTED_PROFILE_MESSAGE(lit)`   | `TracyMessageL(lit)`   | Log line in the timeline             |
| `NOTED_PROFILE_THREAD(lit)`    | `SetThreadName(lit)`   | Once per thread, before any zone     |

The existing `harness::ScopedTimer` and `Counter` are wired in as follows:

- `NOTED_TIMED(label)` expands to both a `ScopedTimer` (publishes a
  wall-clock span via hooks) **and** a Tracy `ZoneScopedN(label)`. One
  call site, two consumers. `label` must be a string literal.
- `Counter::add()` calls `TracyPlot(name, new_value)` after the atomic
  increment. The counter name is forwarded as a `const char*`, so
  counter names must also be string literals (already true everywhere).
- The renderer marks frames at the end of `main()`'s render loop and
  opens named zones around `Renderer::render_frame_with`, `waitForFences`,
  `acquireNextImage`, `queueSubmit2`, `queuePresent`. These are the
  CPU-side stalls we expect to spend money on first.

### Public-API impact

`harness.hpp` now includes `profile.hpp`. `profile.hpp` includes
`tracy/Tracy.hpp` **only** when `NOTED_TRACY_ENABLED` is defined (set as
a `PUBLIC` compile definition on `noted_engine` when the option is on).

When the option is off, no Tracy header is included anywhere in the
project. There is no runtime cost.

When the option is on, every TU that includes `harness.hpp` transitively
sees Tracy. The Tracy header is single-file and inline-only; preprocessor
cost is on the order of ~50 ms per TU.

## Alternatives considered

- **Optick.** Similar API and overhead profile. Less active development
  since 2022; the standalone profiler GUI is Windows-only. Rejected.
- **Superluminal.** Excellent UI, paid, Windows-only. Worth using when
  available but not a substitute for a free in-tree profiler. Use it
  alongside Tracy, not instead of.
- **VTune.** Deep architecture-level profiling. Heavy, slow, the wrong
  granularity for frame-time work. Bring it in only when we need to
  inspect microarchitecture stalls.
- **Roll our own.** ADR 0004 already considered this — the harness is
  the in-app surface. An external profiler is a separate tool. Rejected.
- **Use Tracy unconditionally (drop the build flag).** The headers are
  fine, but the link cost and 200KB binary growth are real. Default-off
  keeps day-to-day builds lean; the CI smoke job ensures the on-path
  doesn't bit-rot.

## Consequences

- Every developer can `cmake -DNOTED_ENABLE_TRACY=ON && cmake --build`
  and connect a Tracy profiler to a running build. Frame view,
  flame graph, plots of every `Counter`, and zero source changes per
  session.
- All existing `NOTED_TIMED` sites and all `Counter::add` calls become
  Tracy-visible automatically. No re-instrumentation required.
- The harness public API now depends on `profile.hpp`. Future profiler
  swaps (or additions — e.g., adding GPU zones via `TracyVkZone`) happen
  inside that one file.
- CI gains a `linux-gcc / Tracy=ON` smoke job. If a code path we
  refactor breaks the Tracy-enabled build, we know in <10 minutes.
- Counter names and `NOTED_TIMED` labels MUST be string literals. This
  was already the de-facto rule (the harness stored them as
  `string_view` of static strings); it is now a documented invariant.

## Follow-ups

- **GPU zones** via `TracyVkZone` — requires a Vulkan-side context
  associated with the command pool. Land alongside `feat/canvas-render-target`
  so the GPU work has zones to label.
- **TracyLockable / TracySharedLockable** wrappers around `std::mutex`
  in the harness once we have real contention to see.
- **Allocation tracking** — Tracy can hook `operator new`/`delete`. Pair
  with VMA budget counters in a later PR.
