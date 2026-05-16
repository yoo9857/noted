#pragma once

// Test harness shared by every test target.
//
//  - golden_image_diff:   compare a rendered tile against a stored PNG.
//                         Threshold-based, used for shader regression.
//  - capture_errors:      scoped subscriber on hook::ErrorObserved that
//                         collects errors so tests can assert on them
//                         without printing to stderr.
//  - reset_global_state:  wipe FeatureFlags / Counters / Config between
//                         tests so they're hermetic.

#include <string>
#include <vector>

#include "noted/engine/error/error.hpp"
#include "noted/engine/hook/registry.hpp"

namespace noted::test {

class ErrorCapture {
public:
    ErrorCapture();
    ~ErrorCapture();

    ErrorCapture(const ErrorCapture&) = delete;
    auto operator=(const ErrorCapture&) -> ErrorCapture& = delete;
    ErrorCapture(ErrorCapture&&) = delete;
    auto operator=(ErrorCapture&&) -> ErrorCapture& = delete;

    [[nodiscard]] auto errors() const -> const std::vector<noted::Error>& { return errors_; }

private:
    std::vector<noted::Error> errors_;
    noted::hook::Token token_ = noted::hook::invalid_token;
};

struct GoldenDiff {
    double max_delta = 0.0;
    double mean_delta = 0.0;
    std::uint64_t pixels_changed = 0;
};

// Real PNG diff lands when feat/gpu-vulkan provides actual rendered output.
auto golden_image_diff(const std::string& candidate_path,
                       const std::string& golden_path) -> noted::Result<GoldenDiff>;

void reset_global_state();

}  // namespace noted::test
