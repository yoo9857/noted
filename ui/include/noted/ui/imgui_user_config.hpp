#pragma once

// Dear ImGui user config — loaded by every ImGui TU via the
// `IMGUI_USER_CONFIG="noted/ui/imgui_user_config.hpp"` define set in
// the root CMakeLists.txt.
//
// Sole purpose: route `IM_ASSERT` through `noted::harness::validate`
// so ImGui's internal invariant violations surface in the same
// observability channel as every other engine assertion (ADR 0003,
// ADR 0027 follow-up).
//
// Forward declarations are used in place of `#include
// "noted/engine/harness/harness.hpp"` to keep the engine's
// hook-registry / chrono / mutex transitive includes out of every
// ImGui translation unit. The full declaration in
// `noted/engine/harness/harness.hpp` and this forward decl must
// have matching signatures (parameter types + default-arg
// presence); see ADR 0003's `validate` contract.

#include <cassert>
#include <source_location>
#include <string_view>

namespace noted::harness {
// Match the canonical signature in noted/engine/harness/harness.hpp.
// The default-arg for `loc` is intentionally NOT repeated here —
// repeating defaults across declarations is a C2572 hard error
// (default argument redefinition). The macro below supplies the
// source location explicitly via `std::source_location::current()`.
void validate(bool cond, std::string_view msg, std::source_location loc);
}  // namespace noted::harness

// IM_ASSERT routing.
//
// On invariant violation we:
//   1) Emit a harness::validate observation — the canonical
//      ErrorObserved channel records the offending expression text
//      and source location.
//   2) Hit assert() so debug builds abort at the violation site
//      (matches ImGui's expected fail-fast contract). NDEBUG strips
//      step 2; only the harness observation remains in Release.
//
// The trade-off (Release builds continue past invariant violations
// with potentially corrupted ImGui state) is intentional for v0.x
// observability. Escalate to unconditional abort once a real user
// payload exists if production telemetry shows it firing.
#define IM_ASSERT(_EXPR)                                                                          \
    do {                                                                                          \
        if (!(_EXPR)) {                                                                           \
            ::noted::harness::validate(false, "ImGui: " #_EXPR, std::source_location::current()); \
            assert(_EXPR);                                                                        \
        }                                                                                         \
    } while (0)
