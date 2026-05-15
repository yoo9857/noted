#pragma once

#include <cstdint>

namespace noted::platform {

enum class PointerKind : std::uint8_t { mouse, pen, touch };

struct PointerEvent {
    PointerKind kind{PointerKind::mouse};
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    bool down = false;
};

}  // namespace noted::platform
