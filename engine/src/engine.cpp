#include "noted/engine/engine.hpp"

#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::engine {

namespace {

harness::Counter ctr_frames_total{"engine.frames.total"};
harness::Counter ctr_init_calls{"engine.lifecycle.init_calls"};
harness::Counter ctr_shutdown_calls{"engine.lifecycle.shutdown_calls"};

}  // namespace

Engine::Engine() = default;

Engine::~Engine() {
    if (initialized_.load(std::memory_order_acquire)) {
        // Best-effort: never throw from a destructor. Failures publish on
        // the error channel so an observer can capture them.
        (void) shutdown();
    }
}

auto Engine::init() -> Result<void> {
    bool expected = false;
    if (!initialized_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        // Idempotent: a second init call is a no-op, not an error.
        return {};
    }
    ctr_init_calls.add();
    hook::registry().on_startup.publish({});
    return {};
}

auto Engine::shutdown() -> Result<void> {
    bool expected = true;
    if (!initialized_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return {};
    }
    ctr_shutdown_calls.add();
    hook::registry().on_shutdown.publish({});
    return {};
}

void Engine::begin_frame() {
    harness::validate(initialized_.load(std::memory_order_acquire),
                      "Engine::begin_frame called before init");
    harness::validate(!in_frame_, "Engine::begin_frame called twice without end_frame");
    in_frame_ = true;
    frame_start_ = std::chrono::steady_clock::now();
    const auto idx = frame_index_.load(std::memory_order_relaxed);
    const auto t_s = std::chrono::duration<double>(frame_start_.time_since_epoch()).count();
    hook::registry().on_frame_begin.publish(hook::FrameBegin{
        .frame_index = idx,
        .time_seconds = t_s,
    });
}

void Engine::end_frame() {
    harness::validate(in_frame_, "Engine::end_frame called without matching begin_frame");
    in_frame_ = false;
    const auto end = std::chrono::steady_clock::now();
    const auto cpu_ms = std::chrono::duration<double, std::milli>(end - frame_start_).count();
    const auto idx = frame_index_.fetch_add(1, std::memory_order_relaxed);
    ctr_frames_total.add();
    hook::registry().on_frame_end.publish(hook::FrameEnd{
        .frame_index = idx,
        .cpu_ms = cpu_ms,
        .gpu_ms = 0.0,
    });
}

}  // namespace noted::engine
