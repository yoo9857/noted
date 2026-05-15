#pragma once

// UI layer. Held thin on purpose: view = pure render of domain state,
// binding = one-way subscription from domain hook channel into view.
// Long-term we may swap implementations (Qt QML vs custom IMGUI vs Slint)
// without touching domain or engine.

#include "noted/ui/binding/binding.hpp"
#include "noted/ui/theme/theme.hpp"
#include "noted/ui/view/view.hpp"
#include "noted/ui/widget/widget.hpp"

namespace noted::ui {}
