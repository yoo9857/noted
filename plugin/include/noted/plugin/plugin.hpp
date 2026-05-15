#pragma once

// WASM plugin host. Third-party filters and tools run inside a wasmtime
// sandbox with capability-based access to the engine API.
//
// Design intent:
//  - plugins cannot directly touch GPU resources; they request operations
//    via the api/ surface, which the host validates and dispatches.
//  - All plugin entry points have time/memory budgets enforced by the host.

#include "noted/plugin/api/api.hpp"
#include "noted/plugin/host/host.hpp"
#include "noted/plugin/sandbox/sandbox.hpp"

namespace noted::plugin {}
