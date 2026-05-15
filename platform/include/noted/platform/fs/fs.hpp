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

}  // namespace noted::platform::fs
