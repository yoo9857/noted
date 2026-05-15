#include "noted/plugin/host/host.hpp"

namespace noted::plugin {

auto Host::load(const std::string&) -> noted::Result<void> {
    return {};  // wasmtime wiring lands in feat/plugin-wasmtime
}
auto Host::unload(const std::string&) -> noted::Result<void> {
    return {};
}

}  // namespace noted::plugin
