#pragma once

#include <string>

#include "noted/engine/error/error.hpp"

namespace noted::plugin {

class Host {
public:
    auto load(const std::string& wasm_path) -> noted::Result<void>;
    auto unload(const std::string& plugin_id) -> noted::Result<void>;
};

}  // namespace noted::plugin
