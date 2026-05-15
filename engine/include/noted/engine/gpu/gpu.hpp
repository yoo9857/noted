#pragma once

// GPU abstraction. Backed by Vulkan today; Metal and DX12 backends slot in
// behind the same interface. All resource handles are opaque ids; the engine
// hides API differences from everything above.

#include <cstdint>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/command_buffer.hpp"
#include "noted/engine/gpu/command_pool.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"

namespace noted::gpu {

enum class Backend : std::uint8_t { vulkan, metal, dx12 };

// Forward-declarations for resource types that land in subsequent PRs.
class Buffer;
class Texture;
class Pipeline;

}  // namespace noted::gpu
