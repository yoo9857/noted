# Architecture Decision Records

ADRs capture cross-cutting decisions: why we chose X, what we rejected, what
the consequences are. They are append-only — when a decision changes, write a
new ADR that supersedes the old one rather than rewriting history.

| #    | Title                                  | Status   |
|------|----------------------------------------|----------|
| 0001 | [C++23 + Vulkan as the engine core](0001-cpp23-vulkan.md) | Accepted |
| 0002 | [Hook system: typed channels with priorities](0002-hook-system.md) | Accepted |
| 0003 | [Error handling: `Result<T>` instead of exceptions](0003-error-handling.md) | Accepted |
| 0004 | [Runtime harness for flexible control](0004-harness.md) | Accepted |
| 0005 | [Module layout: layered C++ libraries](0005-module-layout.md) | Accepted |

## Format

Each ADR has four sections: **Context**, **Decision**, **Alternatives**,
**Consequences**. Keep it short — the goal is a 5-minute read that explains
why a future contributor shouldn't undo the work.
