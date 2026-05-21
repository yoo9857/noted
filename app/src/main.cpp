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

#include <QGuiApplication>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"

#include "app.hpp"
#include "config/app_config.hpp"

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

int main(int argc, char** argv) {
    NOTED_PROFILE_THREAD("main");
    // QGuiApplication is required by every Qt-based windowing path
    // (ADR 0034 phase 1). Constructed on the stack so its lifetime
    // ends after `App` is destroyed — Qt's QWindow must outlive its
    // application instance, and stack-order destruction reverses
    // that into the correct teardown sequence (App dtor first,
    // qt_app dtor second).
    QGuiApplication qt_app(argc, argv);
    qt_app.setApplicationName("noted");
    qt_app.setOrganizationName("noted");

    install_default_observers();

    // Load runtime config: `<exe_dir>/noted.config.json` if present,
    // else `AppConfig::defaults()`. Per ADR 0030, a malformed config
    // logs to stderr and falls through to defaults rather than
    // refusing to launch — the user can still get to a working app.
    auto cfg = noted::app::config::load_default();

    auto app = noted::app::App::create(std::move(cfg));
    if (!app) {
        std::cerr << app.error().format() << '\n';
        return EXIT_FAILURE;
    }
    return (*app)->run();
}
