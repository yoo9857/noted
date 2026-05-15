#pragma once

// Engine-wide hook system.
//
// A Channel<E> is a typed publish/subscribe stream for events of type E.
// Subscribers register a callback with an integer priority (lower = earlier).
// Publishers either dispatch synchronously (publish) or queue for the next
// flush (publish_deferred).
//
// Why types-as-channels:
//   - No string keys, no runtime registry lookup on the hot path.
//   - Compiler enforces signature compatibility between publisher and listener.
//   - Listeners that don't care about an event are linked out entirely.
//
// Why priorities:
//   - The harness wants to wrap every subsystem (logging, profiling, validation)
//     around the rest of the engine. Priorities let the harness sit at the
//     boundary without listeners needing to know about each other.
//
// All callbacks must be `noexcept`-equivalent: if a callback wants to fail,
// it returns a Result<void> via the event payload, never throws.
//
// Rationale: see docs/architecture/0002-hook-system.md.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::hook {

using Token = std::uint64_t;
inline constexpr Token invalid_token = 0;

template <typename Event>
class Channel {
public:
    using Callback = std::function<void(const Event&)>;

    Channel() = default;
    Channel(const Channel&) = delete;
    Channel(Channel&&) noexcept = delete;
    auto operator=(const Channel&) -> Channel& = delete;
    auto operator=(Channel&&) noexcept -> Channel& = delete;
    ~Channel() = default;

    [[nodiscard]] auto subscribe(Callback cb, int priority = 0) -> Token {
        std::scoped_lock lk(mutex_);
        const Token id = ++next_token_;
        listeners_.push_back(Listener{.token = id, .priority = priority, .cb = std::move(cb)});
        std::stable_sort(listeners_.begin(), listeners_.end(),
            [](const Listener& a, const Listener& b) { return a.priority < b.priority; });
        return id;
    }

    void unsubscribe(Token t) noexcept {
        if (t == invalid_token) {
            return;
        }
        std::scoped_lock lk(mutex_);
        std::erase_if(listeners_, [t](const Listener& l) { return l.token == t; });
    }

    void publish(const Event& e) const {
        // Snapshot under lock, dispatch outside. Avoids holding the lock
        // through user callbacks (which may themselves subscribe/unsubscribe).
        std::vector<Callback> snapshot;
        {
            std::scoped_lock lk(mutex_);
            snapshot.reserve(listeners_.size());
            for (const auto& l : listeners_) {
                snapshot.push_back(l.cb);
            }
        }
        for (const auto& cb : snapshot) {
            cb(e);
        }
    }

    void publish_deferred(Event e) {
        std::scoped_lock lk(deferred_mutex_);
        deferred_.push_back(std::move(e));
    }

    void flush() {
        std::vector<Event> drained;
        {
            std::scoped_lock lk(deferred_mutex_);
            drained.swap(deferred_);
        }
        for (const auto& e : drained) {
            publish(e);
        }
    }

    [[nodiscard]] auto listener_count() const noexcept -> std::size_t {
        std::scoped_lock lk(mutex_);
        return listeners_.size();
    }

private:
    struct Listener {
        Token    token{};
        int      priority{};
        Callback cb;
    };

    mutable std::mutex     mutex_;
    std::vector<Listener>  listeners_;
    std::mutex             deferred_mutex_;
    std::vector<Event>     deferred_;
    Token                  next_token_ = 0;
};

// RAII subscription guard — unsubscribes on destruction. Use this in
// subsystem objects so we never leak dangling callbacks on shutdown.
template <typename Event>
class Subscription {
public:
    Subscription() = default;
    Subscription(Channel<Event>& ch, Token t) noexcept : channel_(&ch), token_(t) {}

    Subscription(const Subscription&) = delete;
    auto operator=(const Subscription&) -> Subscription& = delete;

    Subscription(Subscription&& other) noexcept
        : channel_(other.channel_), token_(other.token_) {
        other.channel_ = nullptr;
        other.token_   = invalid_token;
    }
    auto operator=(Subscription&& other) noexcept -> Subscription& {
        if (this != &other) {
            release();
            channel_ = other.channel_;
            token_   = other.token_;
            other.channel_ = nullptr;
            other.token_   = invalid_token;
        }
        return *this;
    }

    ~Subscription() { release(); }

    void release() noexcept {
        if (channel_ != nullptr && token_ != invalid_token) {
            channel_->unsubscribe(token_);
        }
        channel_ = nullptr;
        token_   = invalid_token;
    }

private:
    Channel<Event>* channel_ = nullptr;
    Token           token_   = invalid_token;
};

// ---- Predefined lifecycle events ---------------------------------------
//
// Subsystems hang off these channels via the registry. New event types are
// added by declaring a struct and a getter in registry.hpp.

struct EngineStartup {};
struct EngineShutdown {};

struct FrameBegin {
    std::uint64_t frame_index = 0;
    double        time_seconds = 0.0;
};
struct FrameEnd {
    std::uint64_t frame_index = 0;
    double        cpu_ms = 0.0;
    double        gpu_ms = 0.0;
};

struct DocumentOpened {
    std::string path;
};
struct DocumentSaved {
    std::string path;
};
struct DocumentClosed {};

struct ErrorObserved {
    Error error;
    bool  recoverable = true;
};

struct CommandExecuted {
    const char* name = nullptr;
};

struct TimerSpan {
    std::string_view label;
    double           ms = 0.0;
};

struct FlagChanged {
    std::string_view name;
    bool             new_value = false;
};

// ---- Input events --------------------------------------------------------
//
// Coordinates are in framebuffer pixels with origin at the top-left.
// Pressure is in [0, 1]; 1.0 for mouse and for pen tips that don't report
// pressure. Tilt is in radians, [-pi/2, pi/2]; zero on devices that don't
// report tilt.

enum class PointerButton : std::uint8_t {
    left   = 0,
    right  = 1,
    middle = 2,
    other  = 3,
};

struct PointerMoved {
    double x = 0.0;
    double y = 0.0;
    float  pressure = 1.0F;
    float  tilt_x   = 0.0F;
    float  tilt_y   = 0.0F;
};

struct PointerPressed {
    double        x = 0.0;
    double        y = 0.0;
    PointerButton button = PointerButton::left;
    float         pressure = 1.0F;
};

struct PointerReleased {
    double        x = 0.0;
    double        y = 0.0;
    PointerButton button = PointerButton::left;
};

struct Scrolled {
    double dx = 0.0;
    double dy = 0.0;
};

struct KeyPressed {
    int  glfw_key  = 0;
    int  scancode  = 0;
    int  mods      = 0;  // GLFW_MOD_* bitset
    bool is_repeat = false;
};

struct KeyReleased {
    int glfw_key = 0;
    int scancode = 0;
    int mods     = 0;
};

struct FramebufferResized {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
};

}  // namespace noted::hook
