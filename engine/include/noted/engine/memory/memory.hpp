#pragma once

// Custom allocators. Default allocator is mimalloc when available; subsystems
// build arena/pool allocators on top for hot paths (per-frame, per-tile,
// per-job).
//
// Real implementation lands in feat/memory-arenas.

#include <cstddef>

namespace noted::memory {

class Arena;
class PoolAllocator;

}  // namespace noted::memory
