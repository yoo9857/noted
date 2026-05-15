#pragma once

#include <cstdint>

namespace noted::plugin {

struct Capabilities {
    bool can_read_active_document  = true;
    bool can_write_active_document = false;
    bool can_create_layer          = false;
    bool can_network               = false;
    bool can_filesystem            = false;
    std::uint64_t memory_budget_bytes = 64ull * 1024ull * 1024ull;
    std::uint64_t cpu_budget_ms       = 250;
};

}  // namespace noted::plugin
