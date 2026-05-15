# ADR 0002: Hook system — typed channels with priorities

**Status:** Accepted
**Date:** 2026-05-15

## Context

Cross-cutting concerns (logging, profiling, undo recording, telemetry,
plugins, validation, autosave) must observe engine events without each one
needing direct knowledge of the others. We need:

- compile-time signature safety;
- ordered dispatch so the harness can wrap everything else;
- O(1) cost when no listeners are subscribed.

## Decision

A hook is a `noted::hook::Channel<Event>` where `Event` is any movable struct.
Listeners `subscribe(callback, priority)` and receive events via `publish` (sync)
or `publish_deferred` (queued for `flush`). RAII `Subscription<E>` owns the
subscription token.

A single `Registry` (one instance per process; tests instantiate their own)
holds the well-known channels: `on_startup`, `on_frame_begin`, `on_frame_end`,
`on_document_opened`, `on_error`, `on_command_executed`.

## Alternatives considered

- **String-keyed event bus.** Cheaper to write, but loses compile-time safety
  and adds hash-map lookups to every publish. Rejected.
- **Boost.Signals2 / sigslot.** Heavyweight, exception-throwing, more API
  surface than we need.
- **Virtual interfaces (Observer pattern).** Forces every subsystem to know
  every observer interface; couples publishers to listeners at the type level.
- **Compile-time tag-dispatch only.** Excellent performance but doesn't fit
  plugins that decide what to listen to at runtime.

## Consequences

- New event types are cheap (declare a struct, add a `Channel` to `Registry`).
- Listeners can come and go safely thanks to RAII `Subscription`.
- Callbacks must not throw across module boundaries. They observe a value;
  if they want to flag a problem, they publish to `on_error`.
- Snapshots-then-dispatch means a callback that subscribes during dispatch
  doesn't see the in-flight event — that's the trade-off for re-entrancy
  safety, and it matches how every well-behaved pub-sub system works.
