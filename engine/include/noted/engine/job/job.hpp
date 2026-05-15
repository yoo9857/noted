#pragma once

// Fiber-based job system. One worker per logical core, fibers for cheap
// context switches, lock-free MPMC queue for submission. Modeled on the
// Naughty Dog "parallelizing the engine" talk.
//
// Real implementation lands in feat/job-fibers.

#include <cstddef>
#include <functional>

namespace noted::job {

using JobFn = std::function<void()>;

class Scheduler;

}  // namespace noted::job
