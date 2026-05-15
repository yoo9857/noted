#pragma once

// Stable C ABI exposed to WASM plugins. Versioned. Plugins compiled against
// version N keep working as long as the host advertises N.
//
// All functions take an opaque host handle and return error codes; no
// exceptions, no C++ types cross the boundary.

namespace noted::plugin::api {

inline constexpr int abi_version = 1;

}  // namespace noted::plugin::api
