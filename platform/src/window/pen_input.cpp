#include "noted/platform/window/pen_input.hpp"

#include <algorithm>

namespace noted::platform::pen {

// Cross-platform pure translation. The Win32 / stub TUs supply the
// install / uninstall side; this TU only needs C++ + the hook headers,
// so it compiles cleanly on every target and tests can exercise it
// without any digitizer hardware or even a real window handle.
auto normalize(const RawPenSample& s) noexcept -> noted::hook::PointerMoved {
    // Pens whose hardware caps at 256 or 512 report values inside that
    // range; we'd compress the dynamic range if we divided by `max_seen`,
    // so always divide by 1024. The clamp catches a small handful of
    // drivers that report 1025 on hard pressure.
    constexpr std::uint32_t kFullScale = 1024U;
    const auto pressure_raw = std::min(s.pressure_0_1024, kFullScale);
    const float pressure = static_cast<float>(pressure_raw) /
                           static_cast<float>(kFullScale);

    return noted::hook::PointerMoved{
        .x        = s.client_x,
        .y        = s.client_y,
        .pressure = pressure,
        .tilt_x   = static_cast<float>(s.tilt_x_deg),
        .tilt_y   = static_cast<float>(s.tilt_y_deg),
    };
}

}  // namespace noted::platform::pen
