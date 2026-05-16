#pragma once

// Profiler shim.
//
// Every engine call site uses the NOTED_PROFILE_* macros below instead of
// touching Tracy headers directly. This is the *only* place Tracy.hpp may be
// pulled in, so:
//
//   - Public headers stay independent of the profiler choice.
//   - Build configurations without NOTED_ENABLE_TRACY pay zero include cost.
//   - Swapping profilers (Optick, Superluminal, …) means one file changes.
//
// Macro contract — every macro is a statement form: NOTED_PROFILE_ZONE();
//
//   NOTED_PROFILE_ZONE()                — function-scoped sampling zone.
//   NOTED_PROFILE_ZONE_N(literal)       — named sub-scope (literal C-string).
//   NOTED_PROFILE_FRAME()               — anonymous frame boundary marker.
//   NOTED_PROFILE_FRAME_N(literal)      — named frame (multi-loop apps).
//   NOTED_PROFILE_PLOT(literal, value)  — time series (int64_t/float/double).
//   NOTED_PROFILE_MESSAGE(literal)      — log line in the timeline.
//   NOTED_PROFILE_THREAD(literal)       — label the current thread.
//
// All `literal` arguments must be string literals — Tracy stores them by
// pointer and assumes immortal lifetime. Passing std::string::c_str() is a
// use-after-free in waiting.
//
// Rationale: see docs/architecture/0013-profiling.md.

#if defined(NOTED_TRACY_ENABLED)

#include <tracy/Tracy.hpp>

// clang-format off
#define NOTED_PROFILE_ZONE()                    ZoneScoped
#define NOTED_PROFILE_ZONE_N(name_literal)      ZoneScopedN(name_literal)
#define NOTED_PROFILE_FRAME()                   FrameMark
#define NOTED_PROFILE_FRAME_N(name_literal)     FrameMarkNamed(name_literal)
#define NOTED_PROFILE_PLOT(name_literal, value) TracyPlot(name_literal, value)
#define NOTED_PROFILE_MESSAGE(msg_literal)      TracyMessageL(msg_literal)
#define NOTED_PROFILE_THREAD(name_literal)      ::tracy::SetThreadName(name_literal)
// clang-format on

#else

// clang-format off
#define NOTED_PROFILE_ZONE()                    ((void)0)
#define NOTED_PROFILE_ZONE_N(name_literal)      ((void)0)
#define NOTED_PROFILE_FRAME()                   ((void)0)
#define NOTED_PROFILE_FRAME_N(name_literal)     ((void)0)
#define NOTED_PROFILE_PLOT(name_literal, value) ((void)(value))
#define NOTED_PROFILE_MESSAGE(msg_literal)      ((void)0)
#define NOTED_PROFILE_THREAD(name_literal)      ((void)0)
// clang-format on

#endif
