#pragma once

// Runtime control harness.
//
// The harness exposes the levers that need to be tweaked without rebuilding:
//
//   FeatureFlag      — boolean toggles, declared statically, queried hot
//   Counter          — atomic uint64 counters for cheap perf instrumentation
//   ScopedTimer      — RAII wall-clock timer, publishes to the registry
//   validate / debug_validate — assertion gates with structured error output
//   Config           — string/number/bool registry, intended to be hot-reloaded
//
// The intent is that every subsystem registers its tunables with the harness
// at static init time, and an external tool (or a future debug UI) can list
// them and flip them. Nothing in the harness performs I/O on its own.
//
// Rationale: see docs/architecture/0004-harness.md.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "noted/engine/error/error.hpp"
#include "noted/engine/profile.hpp"

namespace noted::harness {

// ---- Feature flags ------------------------------------------------------

class FeatureFlag {
public:
    FeatureFlag(std::string_view name, bool default_value);

    [[nodiscard]] explicit operator bool() const noexcept {
        return value_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view { return name_; }
    [[nodiscard]] auto default_value() const noexcept -> bool { return default_; }

    // Publishes hook::FlagChanged when the value actually transitions.
    void set(bool v);
    void reset_to_default() { set(default_); }

private:
    std::string_view  name_;
    bool              default_;
    std::atomic<bool> value_;
};

// Look up a registered flag by name. Returns nullptr if unknown.
[[nodiscard]] auto find_flag(std::string_view name) noexcept -> FeatureFlag*;

// Enumerate all registered flags (for tooling/UI).
[[nodiscard]] auto all_flags() -> std::vector<FeatureFlag*>;

// ---- Counters -----------------------------------------------------------
//
// Counter names MUST be string literals (or otherwise static, null-terminated
// storage). The class stores the name as `string_view` and forwards it to the
// profiler verbatim — both consumers assume immortal lifetime + null-termination.

class Counter {
public:
    explicit Counter(std::string_view name);

    void add(std::uint64_t v = 1) noexcept {
        const auto next = value_.fetch_add(v, std::memory_order_relaxed) + v;
        // Tracy plot of the new value. No-op when NOTED_ENABLE_TRACY=OFF.
        NOTED_PROFILE_PLOT(name_.data(), static_cast<std::int64_t>(next));
    }
    void reset() noexcept {
        value_.store(0, std::memory_order_relaxed);
        NOTED_PROFILE_PLOT(name_.data(), static_cast<std::int64_t>(0));
    }
    [[nodiscard]] auto value() const noexcept -> std::uint64_t {
        return value_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view { return name_; }

private:
    std::string_view       name_;
    std::atomic<std::uint64_t> value_{0};
};

[[nodiscard]] auto all_counters() -> std::vector<Counter*>;

// Restore every registered flag to its construction default and zero every
// counter. Intended for test teardown; not safe to call mid-frame.
void reset_all();

// ---- Scoped timer -------------------------------------------------------

class ScopedTimer {
public:
    explicit ScopedTimer(std::string_view label) noexcept;
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    auto operator=(const ScopedTimer&) -> ScopedTimer& = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    auto operator=(ScopedTimer&&) -> ScopedTimer& = delete;

private:
    std::string_view label_;
    std::chrono::steady_clock::time_point start_;
};

#define NOTED_TIMED_CONCAT_INNER(a, b) a##b
#define NOTED_TIMED_CONCAT(a, b) NOTED_TIMED_CONCAT_INNER(a, b)

// NOTED_TIMED(label) — wall-clock RAII scope publishing a TimerSpan AND a
// Tracy zone when NOTED_ENABLE_TRACY=ON. `label` MUST be a string literal —
// Tracy zone names are stored by pointer and assumed to be immortal.
#define NOTED_TIMED(label)                                                          \
    ::noted::harness::ScopedTimer NOTED_TIMED_CONCAT(_noted_timer_, __LINE__)(label); \
    NOTED_PROFILE_ZONE_N(label)

// ---- Validation gates --------------------------------------------------

// In all builds. Use sparingly — it's hot-path acceptable but emits an
// ErrorObserved event when the condition fails.
void validate(
    bool cond,
    std::string_view msg,
    std::source_location loc = std::source_location::current());

// Stripped in NDEBUG builds. Use freely.
#ifdef NDEBUG
inline void debug_validate(bool, std::string_view,
                           std::source_location = std::source_location::current()) noexcept {}
#else
inline void debug_validate(
    bool cond,
    std::string_view msg,
    std::source_location loc = std::source_location::current()) {
    validate(cond, msg, loc);
}
#endif

// ---- Config registry (hot-reloadable knobs) ----------------------------

class Config {
public:
    auto set_bool(std::string key, bool value) -> void;
    auto set_int(std::string key, std::int64_t value) -> void;
    auto set_double(std::string key, double value) -> void;
    auto set_string(std::string key, std::string value) -> void;

    [[nodiscard]] auto get_bool(std::string_view key, bool fallback = false) const -> bool;
    [[nodiscard]] auto get_int(std::string_view key, std::int64_t fallback = 0) const -> std::int64_t;
    [[nodiscard]] auto get_double(std::string_view key, double fallback = 0.0) const -> double;
    [[nodiscard]] auto get_string(std::string_view key, std::string_view fallback = {}) const -> std::string;

    // Replace every entry; intended for hot-reload from a config file.
    void replace_all(Config other);

    static auto instance() -> Config&;

private:
    using Value = std::variant<bool, std::int64_t, double, std::string>;
    mutable std::mutex                       mutex_;
    std::unordered_map<std::string, Value>   entries_;
};

}  // namespace noted::harness
