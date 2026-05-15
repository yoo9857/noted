#include "noted/engine/harness/harness.hpp"

#include <mutex>
#include <unordered_map>

#include "noted/engine/hook/registry.hpp"

namespace noted::harness {

namespace {

struct FlagRegistry {
    std::mutex mu;
    std::unordered_map<std::string_view, FeatureFlag*> by_name;
};

auto flag_registry() -> FlagRegistry& {
    static FlagRegistry r;
    return r;
}

struct CounterRegistry {
    std::mutex mu;
    std::vector<Counter*> all;
};

auto counter_registry() -> CounterRegistry& {
    static CounterRegistry r;
    return r;
}

}  // namespace

// ---- FeatureFlag --------------------------------------------------------

FeatureFlag::FeatureFlag(std::string_view name, bool default_value)
    : name_(name), value_(default_value) {
    auto& r = flag_registry();
    std::scoped_lock lk(r.mu);
    r.by_name.emplace(name, this);
}

auto find_flag(std::string_view name) noexcept -> FeatureFlag* {
    auto& r = flag_registry();
    std::scoped_lock lk(r.mu);
    if (auto it = r.by_name.find(name); it != r.by_name.end()) {
        return it->second;
    }
    return nullptr;
}

auto all_flags() -> std::vector<FeatureFlag*> {
    auto& r = flag_registry();
    std::scoped_lock lk(r.mu);
    std::vector<FeatureFlag*> out;
    out.reserve(r.by_name.size());
    for (auto& [_, f] : r.by_name) {
        out.push_back(f);
    }
    return out;
}

// ---- Counter ------------------------------------------------------------

Counter::Counter(std::string_view name) : name_(name) {
    auto& r = counter_registry();
    std::scoped_lock lk(r.mu);
    r.all.push_back(this);
}

auto all_counters() -> std::vector<Counter*> {
    auto& r = counter_registry();
    std::scoped_lock lk(r.mu);
    return r.all;
}

// ---- ScopedTimer --------------------------------------------------------

ScopedTimer::ScopedTimer(std::string_view label) noexcept
    : label_(label), start_(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    // Timing is observable through hooks. In a real build we'd push to Tracy
    // or our own ring buffer; the structured event keeps tests simple.
    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(end - start_).count();
    (void)label_;
    (void)ms;
    // Intentionally minimal here — wiring to a profiler comes in feat/profile.
}

// ---- validate -----------------------------------------------------------

void validate(bool cond, std::string_view msg, std::source_location loc) {
    if (cond) {
        return;
    }
    hook::registry().on_error.publish(hook::ErrorObserved{
        .error = Error{
            .code    = ErrorCode::invalid_state,
            .message = std::string{msg},
            .where   = loc,
            .cause   = nullptr,
        },
        .recoverable = true,
    });
}

// ---- Config -------------------------------------------------------------

auto Config::instance() -> Config& {
    static Config c;
    return c;
}

void Config::set_bool(std::string key, bool value) {
    std::scoped_lock lk(mutex_);
    entries_[std::move(key)] = value;
}
void Config::set_int(std::string key, std::int64_t value) {
    std::scoped_lock lk(mutex_);
    entries_[std::move(key)] = value;
}
void Config::set_double(std::string key, double value) {
    std::scoped_lock lk(mutex_);
    entries_[std::move(key)] = value;
}
void Config::set_string(std::string key, std::string value) {
    std::scoped_lock lk(mutex_);
    entries_[std::move(key)] = std::move(value);
}

auto Config::get_bool(std::string_view key, bool fallback) const -> bool {
    std::scoped_lock lk(mutex_);
    if (auto it = entries_.find(std::string{key}); it != entries_.end()) {
        if (auto* v = std::get_if<bool>(&it->second)) {
            return *v;
        }
    }
    return fallback;
}
auto Config::get_int(std::string_view key, std::int64_t fallback) const -> std::int64_t {
    std::scoped_lock lk(mutex_);
    if (auto it = entries_.find(std::string{key}); it != entries_.end()) {
        if (auto* v = std::get_if<std::int64_t>(&it->second)) {
            return *v;
        }
    }
    return fallback;
}
auto Config::get_double(std::string_view key, double fallback) const -> double {
    std::scoped_lock lk(mutex_);
    if (auto it = entries_.find(std::string{key}); it != entries_.end()) {
        if (auto* v = std::get_if<double>(&it->second)) {
            return *v;
        }
    }
    return fallback;
}
auto Config::get_string(std::string_view key, std::string_view fallback) const -> std::string {
    std::scoped_lock lk(mutex_);
    if (auto it = entries_.find(std::string{key}); it != entries_.end()) {
        if (auto* v = std::get_if<std::string>(&it->second)) {
            return *v;
        }
    }
    return std::string{fallback};
}

void Config::replace_all(Config other) {
    std::scoped_lock lk(mutex_, other.mutex_);
    entries_ = std::move(other.entries_);
}

}  // namespace noted::harness
