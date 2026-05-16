#pragma once

// Umbrella header for the noted engine module. Pulls in the public API
// surface for downstream consumers (app, plugin host, UI layer).

#include <atomic>
#include <chrono>
#include <cstdint>

#include "noted/engine/color/color.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/gpu.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/hook.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/job/job.hpp"
#include "noted/engine/memory/memory.hpp"
#include "noted/engine/tile/tile.hpp"

namespace noted::engine {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

// Engine owns the run-loop scaffolding: lifecycle transitions, the frame
// counter, and wall-clock frame timing. It does not own GPU resources or
// windows — those are constructed by the app and handed in.
//
// One Engine per process. Lifecycle:
//
//     Engine eng;
//     if (auto r = eng.init(); !r) ...;
//     while (running) {
//         eng.begin_frame();
//         ...                          // app work
//         eng.end_frame();
//     }
//     eng.shutdown();
//
// init/shutdown are idempotent. Calling begin_frame before init or after
// shutdown is a programming error and is caught by harness::validate.
class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    auto operator=(const Engine&) -> Engine& = delete;
    auto operator=(Engine&&) -> Engine& = delete;

    [[nodiscard]] auto init() -> Result<void>;
    [[nodiscard]] auto shutdown() -> Result<void>;

    void begin_frame();
    void end_frame();

    [[nodiscard]] auto frame_index() const noexcept -> std::uint64_t {
        return frame_index_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto is_initialized() const noexcept -> bool {
        return initialized_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> initialized_{false};
    std::atomic<std::uint64_t> frame_index_{0};
    std::chrono::steady_clock::time_point frame_start_{};
    bool in_frame_ = false;
};

}  // namespace noted::engine
