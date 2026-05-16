#pragma once

// Single point of access to all engine-wide hook channels.
//
// Why one Registry instead of many globals: makes the wiring testable.
// In tests we spin up a fresh Registry; in production there's exactly one
// owned by the Engine root object.

#include "noted/engine/hook/hook.hpp"

namespace noted::hook {

class Registry {
public:
    Channel<EngineStartup> on_startup;
    Channel<EngineShutdown> on_shutdown;
    Channel<FrameBegin> on_frame_begin;
    Channel<FrameEnd> on_frame_end;
    Channel<DocumentOpened> on_document_opened;
    Channel<DocumentSaved> on_document_saved;
    Channel<DocumentClosed> on_document_closed;
    Channel<ErrorObserved> on_error;
    Channel<CommandExecuted> on_command_executed;
    Channel<TimerSpan> on_timer_span;
    Channel<FlagChanged> on_flag_changed;

    // Input.
    Channel<PointerMoved> on_pointer_moved;
    Channel<PointerPressed> on_pointer_pressed;
    Channel<PointerReleased> on_pointer_released;
    Channel<Scrolled> on_scrolled;
    Channel<KeyPressed> on_key_pressed;
    Channel<KeyReleased> on_key_released;
    Channel<FramebufferResized> on_framebuffer_resized;
};

// Access the process-wide Registry. App owns its lifetime via construct/destroy.
[[nodiscard]] auto registry() -> Registry&;

}  // namespace noted::hook
