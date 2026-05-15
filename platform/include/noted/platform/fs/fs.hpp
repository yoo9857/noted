#pragma once

#include <filesystem>

#include "noted/engine/error/error.hpp"

namespace noted::platform::fs {

auto read_all(const std::filesystem::path& p) -> noted::Result<std::string>;
auto write_all(const std::filesystem::path& p, std::string_view content) -> noted::Result<void>;

}  // namespace noted::platform::fs
