#pragma once

#include "noted/engine/error/error.hpp"

namespace noted::domain {

// Every state mutation is a Command. Commands form the undo/redo stack and
// the CRDT replication wire format. Pure-function `apply`/`revert` so the
// engine can replay them deterministically.
class Command {
public:
    virtual ~Command() = default;
    virtual auto apply()  -> noted::Result<void> = 0;
    virtual auto revert() -> noted::Result<void> = 0;
    virtual auto name() const -> const char* = 0;
};

}  // namespace noted::domain
