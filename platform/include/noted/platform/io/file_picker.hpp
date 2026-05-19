#pragma once

// Native file open / save dialogs for the `.noted` archive format.
//
// Thin wrapper around nativefiledialog-extended. The wrapper hides
// NFD's C handle types, owns NFD_Init / NFD_Quit per call (cheap,
// also re-entrant-safe), and normalizes the three NFD outcomes into
// a single `Result<std::optional<path>>`:
//
//   - user picked a file → `path`
//   - user cancelled     → `std::nullopt` (not an error)
//   - dialog failed      → `Result` error
//
// All entry points are blocking and must be called from the same
// thread that pumps GLFW events — NFD installs a modal loop on
// Windows and macOS. Filter is hard-coded to `.noted` because the
// only caller today is the File menu of the product shell; if a
// second caller needs a different filter, lift it to a parameter.

#include <filesystem>
#include <optional>
#include <string_view>

#include "noted/engine/error/error.hpp"

namespace noted::platform::io {

// Show the OS open dialog filtered to `.noted` archives. Returns the
// picked path, or `std::nullopt` if the user cancelled. Errors only
// for NFD-internal failures (COM init, GTK init, etc.).
[[nodiscard]] auto pick_noted_open() -> Result<std::optional<std::filesystem::path>>;

// Show the OS save dialog filtered to `.noted` archives. `default_name`
// pre-fills the filename field (extension is appended automatically by
// the OS dialog when missing).
[[nodiscard]] auto pick_noted_save(std::string_view default_name)
    -> Result<std::optional<std::filesystem::path>>;

}  // namespace noted::platform::io
