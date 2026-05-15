# ADR 0004: Runtime harness for flexible control

**Status:** Accepted
**Date:** 2026-05-15

## Context

A long-lived AAA app needs to ship dark experiments, turn off subsystems
when they misbehave, and let an operator tune thresholds without a rebuild.
We also want one place to read perf counters from, so debug UI and CI
benchmarks share the same source of truth.

## Decision

The `noted::harness` namespace owns four primitives:

| Primitive       | Use                                                            |
|-----------------|----------------------------------------------------------------|
| `FeatureFlag`   | named bool, defaulted at declaration, hot-toggleable           |
| `Counter`       | atomic uint64, registered at construction, listable by tooling |
| `ScopedTimer`   | RAII wall-clock timer, integrates with profiler hook later     |
| `validate(...)` | assertion gate that publishes on `hook::on_error`              |

A `harness::Config` registry exposes `bool/int/double/string` values that can
be `replace_all`'d at runtime — that's the hot-reload path for tunables
loaded from a TOML/JSON file.

Every subsystem declares its knobs as static globals at the top of its
translation unit. The harness sees them through static-init registration.

## Alternatives considered

- **Compile-time only flags.** Inflexible — every experiment needs a rebuild.
- **External flag service.** Network-dependent; wrong layer for a desktop app.
- **No harness — just `#ifdef DEBUG`.** Loses runtime steering and is opaque
  to operators.

## Consequences

- Subsystems get a uniform observation surface — one tool can list every
  flag, counter, and config key in the process.
- Static-init order matters once subsystems start reading other subsystems'
  flags at init. We enforce: flags may be *declared* at static init, but
  must not be *read* at static init.
- Tests are responsible for resetting global state via `noted::test::reset_global_state()`.
