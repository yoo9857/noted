#pragma once

// Per-OS abstractions: window/surface creation, input event pump, file system
// watchers, IPC. Everything above this line stays platform-agnostic.

#include "noted/platform/fs/fs.hpp"
#include "noted/platform/input/input.hpp"
#include "noted/platform/ipc/ipc.hpp"
#include "noted/platform/window/window.hpp"

namespace noted::platform {}
