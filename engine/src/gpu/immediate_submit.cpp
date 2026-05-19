#include "noted/engine/gpu/immediate_submit.hpp"

#include <string>
#include <utility>

#include "noted/engine/gpu/command_buffer.hpp"
#include "noted/engine/gpu/command_pool.hpp"

namespace noted::gpu {

auto immediate_submit(const Device& device,
                      const ImmediateSubmitInfo& info,
                      const ImmediateRecordFn& record) -> Result<void> {
    if (info.queue == VK_NULL_HANDLE || info.queue_family == UINT32_MAX) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "immediate_submit: ImmediateSubmitInfo.queue and queue_family must be set"));
    }
    if (!record) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "immediate_submit: record fn is empty"));
    }

    auto pool =
        CommandPool::create(device, info.queue_family, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    if (!pool) {
        return std::unexpected(std::move(pool).error());
    }
    auto cb = CommandBuffer::allocate(*pool);
    if (!cb) {
        return std::unexpected(std::move(cb).error());
    }
    if (auto r = cb->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); !r) {
        return std::unexpected(std::move(r).error());
    }

    record(cb->handle());

    if (auto r = cb->end(); !r) {
        return std::unexpected(std::move(r).error());
    }

    // One-shot fence — created and destroyed inside this call so the
    // helper has no persistent GPU state.
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence done = VK_NULL_HANDLE;
    if (auto vr = vkCreateFence(device.handle(), &fci, nullptr, &done); vr != VK_SUCCESS) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_validation_failed,
                              std::string{"vkCreateFence(immediate_submit): VkResult="} +
                                  std::to_string(static_cast<int>(vr))));
    }

    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = cb->handle();

    VkSubmitInfo2 si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cb_info;

    if (auto vr = vkQueueSubmit2(info.queue, 1, &si, done); vr != VK_SUCCESS) {
        vkDestroyFence(device.handle(), done, nullptr);
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_validation_failed,
                              std::string{"vkQueueSubmit2(immediate_submit): VkResult="} +
                                  std::to_string(static_cast<int>(vr))));
    }

    // Block until the GPU has consumed the work. UINT64_MAX is the
    // "wait forever" sentinel; a real timeout policy can be threaded
    // through ImmediateSubmitInfo if a caller needs it.
    (void) vkWaitForFences(device.handle(), 1, &done, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device.handle(), done, nullptr);
    return {};
}

}  // namespace noted::gpu
