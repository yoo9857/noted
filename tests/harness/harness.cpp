#include "harness.hpp"

#include <utility>

namespace noted::test {

ErrorCapture::ErrorCapture() {
    token_ = noted::hook::registry().on_error.subscribe(
        [this](const noted::hook::ErrorObserved& e) {
            errors_.push_back(e.error);
        });
}

ErrorCapture::~ErrorCapture() {
    noted::hook::registry().on_error.unsubscribe(token_);
}

auto golden_image_diff(const std::string&, const std::string&) -> noted::Result<GoldenDiff> {
    return std::unexpected(noted::make_error(
        noted::ErrorCode::not_implemented,
        "golden_image_diff: stub — wired in feat/gpu-vulkan"));
}

void reset_global_state() {
    // Placeholder: enumerating FeatureFlags via all_flags() and resetting
    // them to their construction defaults is added once we record those.
}

}  // namespace noted::test
