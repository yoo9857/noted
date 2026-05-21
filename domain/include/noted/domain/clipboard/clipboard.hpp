#pragma once

// Clipboard — session-level scratch storage for primitives copied
// out of the document by Cut / Copy commands.
//
// Pure data store. The Clipboard itself does no I/O, no GPU, no UI;
// it just owns a vector of primitives. The Cut / Copy / Paste
// commands in `domain/command/commands.hpp` use it as their
// destination / source. App keyboard shortcuts (Ctrl+X / C / V) call
// `session_.execute(...)` on those commands.
//
// **MVP scope (v1):** shape primitives only. Text / image / stroke
// extension lands in follow-up PRs once the selection model gains
// a unified "primitives intersecting selection" helper. For shapes
// alone the bounds are well-defined (rectangular bbox) so the
// selection-test is unambiguous.
//
// Lifecycle: one Clipboard per `DocumentSession` (created when the
// session is). The clipboard survives Document open / save / close
// — that's the user's mental model ("I copied that, it should
// still be there after I open the next file"). On process exit
// the clipboard is discarded; OS-level pasteboard sync is a
// future slice.

#include <vector>

#include "noted/domain/tool/shape_drag.hpp"

namespace noted::domain {

class Clipboard {
public:
    Clipboard() = default;

    void clear() noexcept { shapes_.clear(); }

    void set_shapes(std::vector<tool::ShapePrimitive> shapes) noexcept {
        shapes_ = std::move(shapes);
    }

    [[nodiscard]] auto shapes() const noexcept -> const std::vector<tool::ShapePrimitive>& {
        return shapes_;
    }

    [[nodiscard]] auto has_shapes() const noexcept -> bool { return !shapes_.empty(); }

    [[nodiscard]] auto is_empty() const noexcept -> bool { return shapes_.empty(); }

private:
    std::vector<tool::ShapePrimitive> shapes_;
};

}  // namespace noted::domain
