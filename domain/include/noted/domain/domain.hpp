#pragma once

// Domain layer: document model, layer graph, command pattern, CRDT.
// Pure logic — no GPU, no platform calls, no I/O.

#include "noted/domain/command/command.hpp"
#include "noted/domain/crdt/crdt.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/domain/layer/layer.hpp"

namespace noted::domain {}
