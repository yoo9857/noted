#pragma once

// Project-wide error model.
//
//  - No exceptions cross module boundaries. Every fallible function returns
//    Result<T> = std::expected<T, Error>.
//  - Error carries a code, a human message, the source_location where it was
//    constructed, and an optional `cause` for chaining.
//  - Use NOTED_TRY(expr) in functions returning Result<U> to propagate.
//
// Rationale: see docs/architecture/0003-error-handling.md.

#include <cstdint>
#include <expected>
#include <memory>
#include <source_location>
#include <string>
#include <utility>

namespace noted {

enum class ErrorCode : std::uint16_t {
    ok = 0,

    // Generic
    unknown,
    cancelled,
    not_implemented,
    invalid_argument,
    invalid_state,
    out_of_memory,
    timeout,

    // I/O
    file_not_found,
    permission_denied,
    io_failure,

    // GPU
    gpu_device_lost,
    gpu_out_of_memory,
    gpu_surface_lost,
    gpu_shader_compile_failed,
    gpu_validation_failed,
    gpu_swapchain_out_of_date,  // recoverable: rebuild swapchain
    gpu_swapchain_suboptimal,   // recoverable: rebuild swapchain at convenience

    // Document / domain
    document_corrupt,
    document_version_unsupported,
    invalid_image_format,
    color_profile_invalid,
    command_failed,
    crdt_merge_failed,

    // Plugin
    plugin_load_failed,
    plugin_sandbox_violation,
    plugin_budget_exceeded,
};

[[nodiscard]] constexpr auto to_string(ErrorCode c) noexcept -> const char* {
    switch (c) {
        case ErrorCode::ok:
            return "ok";
        case ErrorCode::unknown:
            return "unknown";
        case ErrorCode::cancelled:
            return "cancelled";
        case ErrorCode::not_implemented:
            return "not_implemented";
        case ErrorCode::invalid_argument:
            return "invalid_argument";
        case ErrorCode::invalid_state:
            return "invalid_state";
        case ErrorCode::out_of_memory:
            return "out_of_memory";
        case ErrorCode::timeout:
            return "timeout";
        case ErrorCode::file_not_found:
            return "file_not_found";
        case ErrorCode::permission_denied:
            return "permission_denied";
        case ErrorCode::io_failure:
            return "io_failure";
        case ErrorCode::gpu_device_lost:
            return "gpu_device_lost";
        case ErrorCode::gpu_out_of_memory:
            return "gpu_out_of_memory";
        case ErrorCode::gpu_surface_lost:
            return "gpu_surface_lost";
        case ErrorCode::gpu_shader_compile_failed:
            return "gpu_shader_compile_failed";
        case ErrorCode::gpu_validation_failed:
            return "gpu_validation_failed";
        case ErrorCode::gpu_swapchain_out_of_date:
            return "gpu_swapchain_out_of_date";
        case ErrorCode::gpu_swapchain_suboptimal:
            return "gpu_swapchain_suboptimal";
        case ErrorCode::document_corrupt:
            return "document_corrupt";
        case ErrorCode::document_version_unsupported:
            return "document_version_unsupported";
        case ErrorCode::invalid_image_format:
            return "invalid_image_format";
        case ErrorCode::color_profile_invalid:
            return "color_profile_invalid";
        case ErrorCode::command_failed:
            return "command_failed";
        case ErrorCode::crdt_merge_failed:
            return "crdt_merge_failed";
        case ErrorCode::plugin_load_failed:
            return "plugin_load_failed";
        case ErrorCode::plugin_sandbox_violation:
            return "plugin_sandbox_violation";
        case ErrorCode::plugin_budget_exceeded:
            return "plugin_budget_exceeded";
    }
    return "<invalid ErrorCode>";
}

struct Error {
    ErrorCode code = ErrorCode::unknown;
    std::string message;
    std::source_location where = std::source_location::current();
    std::shared_ptr<const Error> cause;  // shared so Result<T> stays copyable

    [[nodiscard]] auto format() const -> std::string;
};

template <typename T>
using Result = std::expected<T, Error>;

// Make an Error inline; callers don't have to repeat source_location.
[[nodiscard]] inline auto make_error(ErrorCode code,
                                     std::string message,
                                     std::source_location loc = std::source_location::current())
    -> Error {
    return Error{.code = code, .message = std::move(message), .where = loc, .cause = nullptr};
}

[[nodiscard]] inline auto chain_error(Error new_top, Error cause) -> Error {
    new_top.cause = std::make_shared<const Error>(std::move(cause));
    return new_top;
}

// Propagate an error from a Result-returning expression. Equivalent to Rust's `?`.
// Usage (GCC/Clang only — relies on the statement-expression extension):
//
//     auto value = NOTED_TRY(some_call());
//
// On MSVC the macro is intentionally not defined. Use std::expected's monadic
// API instead:
//
//     return some_call()
//         .and_then([](auto v) -> Result<U> { return next(v); });
//
// We prefer the monadic form everywhere for portable code; NOTED_TRY exists
// only because it makes deeply nested call chains tolerable on Linux.
#if defined(__GNUC__) || defined(__clang__)
#define NOTED_TRY(expr)                                              \
    ({                                                               \
        auto&& _noted_r = (expr);                                    \
        if (!_noted_r) {                                             \
            return ::std::unexpected(::std::move(_noted_r).error()); \
        }                                                            \
        ::std::move(_noted_r).value();                               \
    })
#endif

}  // namespace noted
