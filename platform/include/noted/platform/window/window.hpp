#pragma once

#include <cstdint>
#include <string_view>

namespace noted::platform {

struct WindowDesc {
    std::string_view title{"noted"};
    std::uint32_t width  = 1600;
    std::uint32_t height = 1000;
    bool resizable = true;
    bool high_dpi  = true;
};

class Window;

}  // namespace noted::platform
