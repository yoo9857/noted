// Application entry. Three responsibilities only:
//   1. Profile thread name (for Tracy)
//   2. Install the process-lifetime hook observers (stderr error
//      logging, stdout frame ticker, pointer + resize traces)
//   3. Construct + run the App
//
// Everything else — engine init, GPU stack, scene, UI session,
// per-frame loop — lives in `app/src/app.{hpp,cpp}` so each
// responsibility is owned by a named type instead of a 800-line
// `main()`. See `app::App::create()`.

#include <cstdlib>
#include <iostream>

#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"

#include "app.hpp"

namespace {

void install_default_observers() {
    auto& reg = noted::hook::registry();
    (void) reg.on_error.subscribe([](const noted::hook::ErrorObserved& e) {
        std::cerr << "[error] " << e.error.format() << '\n';
    });
    (void) reg.on_frame_end.subscribe([](const noted::hook::FrameEnd& f) {
        if ((f.frame_index % 240) == 0) {
            std::cout << "frame " << f.frame_index << " | cpu " << f.cpu_ms << " ms\n";
        }
    });
    (void) reg.on_pointer_pressed.subscribe([](const noted::hook::PointerPressed& p) {
        std::cout << "pointer down " << p.x << ", " << p.y << " btn=" << static_cast<int>(p.button)
                  << '\n';
    });
    (void) reg.on_framebuffer_resized.subscribe([](const noted::hook::FramebufferResized& r) {
        std::cout << "framebuffer resized to " << r.width << "x" << r.height << '\n';
    });
}

}  // namespace

int main() {
    NOTED_PROFILE_THREAD("main");
    install_default_observers();

    auto app = noted::app::App::create();
    if (!app) {
        std::cerr << app.error().format() << '\n';
        return EXIT_FAILURE;
    }
    return (*app)->run();
}
