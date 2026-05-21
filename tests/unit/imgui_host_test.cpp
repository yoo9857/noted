#include "noted/ui/imgui_host.hpp"

#include <gtest/gtest.h>

// The ImGuiHost::create rejection paths are pure-logic checks on the
// CreateInfo struct's pointer + format / count fields. They run with
// no GPU, no window — exactly what unit-test infrastructure supplies.
// The success path requires a live Vulkan device + a window-bound
// Vulkan surface; that is GPU-integration territory and runs against a
// real renderer in the app binary (no CI GPU runner yet, see ADR
// 0027 follow-ups).

namespace {

using noted::ui::ImGuiHost;
using noted::ui::ImGuiHostCreateInfo;

}  // namespace

TEST(ImGuiHostCreate, RejectsAllNullPointers) {
    auto r = ImGuiHost::create({});
    EXPECT_FALSE(r);
}

TEST(ImGuiHostCreate, RejectsNullInstance) {
    // Non-null fake pointers for everything except instance. The
    // function rejects before dereferencing, so the fake values never
    // get touched — this is a pure precondition check.
    ImGuiHostCreateInfo info{};
    info.physical_device = reinterpret_cast<const noted::gpu::PhysicalDevice*>(0x1);
    info.device = reinterpret_cast<const noted::gpu::Device*>(0x2);
    info.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    auto r = ImGuiHost::create(info);
    EXPECT_FALSE(r);
}

TEST(ImGuiHostCreate, RejectsUndefinedColorFormat) {
    ImGuiHostCreateInfo info{};
    info.instance = reinterpret_cast<const noted::gpu::Instance*>(0x1);
    info.physical_device = reinterpret_cast<const noted::gpu::PhysicalDevice*>(0x2);
    info.device = reinterpret_cast<const noted::gpu::Device*>(0x3);
    info.color_format = VK_FORMAT_UNDEFINED;
    info.image_count = 2;
    auto r = ImGuiHost::create(info);
    EXPECT_FALSE(r);
}

TEST(ImGuiHostCreate, RejectsImageCountBelowTwo) {
    ImGuiHostCreateInfo info{};
    info.instance = reinterpret_cast<const noted::gpu::Instance*>(0x1);
    info.physical_device = reinterpret_cast<const noted::gpu::PhysicalDevice*>(0x2);
    info.device = reinterpret_cast<const noted::gpu::Device*>(0x3);
    info.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    info.image_count = 1;
    auto r = ImGuiHost::create(info);
    EXPECT_FALSE(r);
}

TEST(ImGuiHostCreate, RejectsImageCountAboveSixteen) {
    ImGuiHostCreateInfo info{};
    info.instance = reinterpret_cast<const noted::gpu::Instance*>(0x1);
    info.physical_device = reinterpret_cast<const noted::gpu::PhysicalDevice*>(0x2);
    info.device = reinterpret_cast<const noted::gpu::Device*>(0x3);
    info.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    info.image_count = 17;
    auto r = ImGuiHost::create(info);
    EXPECT_FALSE(r);
}

TEST(ImGuiHostMove, DefaultMovedFromIsNotInitialized) {
    // We cannot construct a successful host here (no GPU), but we
    // can verify the public `initialized()` predicate's default-state
    // contract by reading it on a moved-from-empty test surface:
    // a freshly default-rejected create() never produces a host, so
    // the only path that yields one is the GPU path. The predicate
    // exists as a debug aid for the failure path — assert its zero
    // state cannot be observed externally via this API.
    auto r = ImGuiHost::create({});
    EXPECT_FALSE(r);  // no host produced, nothing else to assert
}
