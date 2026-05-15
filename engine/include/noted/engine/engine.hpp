#pragma once

// Umbrella header for the noted engine module. Pulls in the public API
// surface for downstream consumers (app, plugin host, UI layer).

#include "noted/engine/color/color.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/gpu.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/hook.hpp"
#include "noted/engine/job/job.hpp"
#include "noted/engine/memory/memory.hpp"
#include "noted/engine/tile/tile.hpp"

namespace noted::engine {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

}  // namespace noted::engine
