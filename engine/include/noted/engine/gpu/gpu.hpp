#pragma once

// GPU abstraction. Backed by Vulkan today; Metal and DX12 backends slot in
// behind the same interface. All resource handles are opaque ids; the engine
// hides API differences from everything above.
//
// Real implementation lands in feat/gpu-vulkan.

#include <cstdint>

#include "noted/engine/error/error.hpp"

namespace noted::gpu {

enum class Backend : std::uint8_t { vulkan, metal, dx12 };

struct DeviceInfo {
    std::uint32_t api_version  = 0;
    std::uint32_t driver_version = 0;
    char          name[256]    = {};
    bool          discrete     = false;
};

class Device;
class CommandQueue;
class CommandBuffer;
class Buffer;
class Texture;
class Pipeline;

}  // namespace noted::gpu
