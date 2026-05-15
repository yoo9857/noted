# ADR 0006: Vulkan resource ownership — move-only RAII

**Status:** Accepted
**Date:** 2026-05-15

## Context

Vulkan exposes raw `VkInstance`, `VkDevice`, `VkSwapchainKHR`, `VkImage`, etc.
as opaque handles with manual lifecycle (`vkCreate.../vkDestroy...`). Mistakes
are silent: double-destroy, use-after-free, leaked handles when an exception
unwinds. C++ RAII is the only sane way to manage this at AAA scale, but we
have to choose how.

## Decision

Every Vulkan handle is wrapped in a **move-only** C++ class with three
properties:

1. **No public constructor.** Construction goes through a static
   `create(...)` factory returning `Result<T>`. Half-constructed objects are
   impossible.
2. **No copy.** Copying a `VkInstance` would create two destroyers for one
   handle. The compiler enforces this with `= delete`.
3. **Destructor calls `vkDestroy...`.** Always, even on exception unwind.
   Members are reset to `VK_NULL_HANDLE` after destruction so a moved-from
   object's destructor is a no-op.

The raw handle is exposed via `handle() const`. Advanced callers (e.g.
custom render passes) can call Vulkan directly with it; they must not call
`vkDestroy...` on it.

```cpp
auto inst = Instance::create({.enable_validation = true, ...});
if (!inst) return std::unexpected(std::move(inst).error());

VkInstance raw = inst->handle();   // borrow, never own
auto picked = PhysicalDevice::select(*inst);
```

## Alternatives considered

- **`std::unique_ptr<VkInstance_T, Deleter>`.** Forces a custom deleter type
  per resource and offers no place to keep auxiliary state (e.g. the debug
  messenger paired with the instance). Rejected.
- **`vulkan-hpp` RAII.** Excellent quality, but ties us to its dispatcher
  init pattern and re-encodes every Vulkan enum as a C++ enum class — we'd
  end up writing translation glue at every boundary with raw Vulkan code
  (validation callbacks, NVAPI, etc.). Reserved for a future evaluation.
- **Shared ownership (`std::shared_ptr`).** GPU resources have a clear single
  owner: the subsystem that created them. Reference counting hides leaks
  and adds atomic ops to a hot path. Rejected.

## Consequences

- The compiler refuses to compile common mistakes (copy of an Instance,
  losing the return value of `create`).
- Every resource integrates with `Result<T>` and the error hook channel.
- Adding a new resource is mechanical: define the class, the factory, the
  destructor, and the move members. Pattern is identical across types,
  which keeps reviews fast.
- We pay a tiny cost: `create` must return by value, which requires the
  type to be move-constructible. No measurable runtime cost.
