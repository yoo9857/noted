#include "noted/engine/hook/registry.hpp"

namespace noted::hook {

namespace {
Registry g_registry;
}  // namespace

auto registry() -> Registry& { return g_registry; }

}  // namespace noted::hook
