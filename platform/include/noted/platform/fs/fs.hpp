#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "noted/engine/error/error.hpp"

namespace noted::platform::fs {

auto read_all(const std::filesystem::path& p) -> noted::Result<std::string>;
auto write_all(const std::filesystem::path& p, std::string_view content) -> noted::Result<void>;

// Read a SPIR-V file as 32-bit words. Validates the SPIR-V magic number
// (0x07230203) and rejects files whose byte length is not a multiple of 4.
auto read_spirv(const std::filesystem::path& p) -> noted::Result<std::vector<std::uint32_t>>;

// Directory containing the currently-running executable.
//
// Used by the app's asset-path resolver to look for `shaders/` and
// `noted.config.json` next to the binary in the production layout.
// Implementation:
//   - Win32  : `GetModuleFileNameW`
//   - macOS  : `_NSGetExecutablePath`
//   - Linux  : `readlink(/proc/self/exe)`
//
// Returns an empty path on any failure (rare — would mean the OS
// can't tell us where our own binary lives). Callers should treat
// empty as "fall through to the next candidate in the search path"
// rather than propagating an error.
[[nodiscard]] auto executable_dir() -> std::filesystem::path;

}  // namespace noted::platform::fs
