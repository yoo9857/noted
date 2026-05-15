# ADR 0003: Error handling — `Result<T>`, not exceptions

**Status:** Accepted
**Date:** 2026-05-15

## Context

Image pipelines fail in predictable, recoverable ways (missing files,
unsupported color profiles, GPU device lost, plugin sandbox violations).
Exceptions across module boundaries make control flow invisible and can
cause undefined behavior across an FFI / WASM / plugin boundary.

## Decision

Every fallible public function returns `noted::Result<T>`, which is an alias
for `std::expected<T, noted::Error>`. `Error` carries:

- a typed `ErrorCode` enum;
- a human message;
- the `std::source_location` of the call site;
- an optional `cause` chain.

Errors propagate with `NOTED_TRY(expr)` (statement-expression on GCC/Clang).
The hook system has an `on_error` channel; subsystems publish there instead
of `std::cerr` so observability is centralized.

Exceptions are still permitted inside a single translation unit when calling
the standard library, but they must be caught before crossing the
module boundary and converted to `Result<T>`.

## Alternatives considered

- **Plain exceptions everywhere.** Invisible control flow, ABI fragility,
  no propagation across C/WASM boundaries.
- **C-style integer return codes.** Loses type information for the payload.
- **`std::error_code` + outparams.** Verbose; `std::expected` subsumes it.
- **`absl::Status`.** Adds a dependency for the same shape.

## Consequences

- Callers see at the type level which functions can fail.
- `Error::format()` produces a chained trace that we can log or surface
  in the UI without throwing anything away.
- Compile-time cost: `Result<T>` is one branch on the happy path; negligible.
- Programmer cost: explicit `if (!r)` checks. Net positive.
