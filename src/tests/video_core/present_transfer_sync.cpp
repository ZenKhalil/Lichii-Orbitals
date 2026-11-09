// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/present/util.h"

namespace {

enum class Command { Barrier, Clear };

struct Event {
    Command command{};
    VkImageMemoryBarrier2 barrier{};
    VkImage image{};
    VkImageLayout layout{};
    VkClearColorValue color{};
    VkImageSubresourceRange range{};
};

// Capture the real command wrapper's dispatch without a Vulkan device or driver.
// Fixed storage keeps allocations and assertions out of the API callbacks.
struct Capture {
    std::array<Event, 8> events{};
    std::size_t count{};
    bool malformed{};
    unsigned legacy_calls{};
    unsigned sync2_calls{};

    void Append(const Event& event) {
        if (count == events.size()) {
            malformed = true;
            return;
        }
        events[count++] = event;
    }
};

thread_local Capture* active_capture{};

template <typename Handle>
Handle FakeHandle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

VKAPI_ATTR void VKAPI_CALL RecordBarrier2(VkCommandBuffer, const VkDependencyInfo* info) {
    auto& capture = *active_capture;
    ++capture.sync2_calls;
    if (info->sType != VK_STRUCTURE_TYPE_DEPENDENCY_INFO || info->dependencyFlags != 0 ||
        info->memoryBarrierCount != 0 || info->bufferMemoryBarrierCount != 0 ||
        info->imageMemoryBarrierCount != 1 || info->pImageMemoryBarriers == nullptr) {
        capture.malformed = true;
        return;
    }
    capture.Append({.command = Command::Barrier, .barrier = info->pImageMemoryBarriers[0]});
}

VKAPI_ATTR void VKAPI_CALL RecordBarrier(VkCommandBuffer, VkPipelineStageFlags source_stage,
                                        VkPipelineStageFlags target_stage, VkDependencyFlags flags,
                                        std::uint32_t memory_count, const VkMemoryBarrier*,
                                        std::uint32_t buffer_count, const VkBufferMemoryBarrier*,
                                        std::uint32_t image_count,
                                        const VkImageMemoryBarrier* images) {
    auto& capture = *active_capture;
    ++capture.legacy_calls;
    if (flags != 0 || memory_count != 0 || buffer_count != 0 || image_count != 1 || images == nullptr) {
        capture.malformed = true;
        return;
    }
    const auto& image = images[0];
    capture.malformed |= image.sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    capture.Append({.command = Command::Barrier,
                    .barrier = {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = image.pNext,
                        .srcStageMask = source_stage,
                        .srcAccessMask = image.srcAccessMask,
                        .dstStageMask = target_stage,
                        .dstAccessMask = image.dstAccessMask,
                        .oldLayout = image.oldLayout,
                        .newLayout = image.newLayout,
                        .srcQueueFamilyIndex = image.srcQueueFamilyIndex,
                        .dstQueueFamilyIndex = image.dstQueueFamilyIndex,
                        .image = image.image,
                        .subresourceRange = image.subresourceRange,
                    }});
}

VKAPI_ATTR void VKAPI_CALL RecordClear(VkCommandBuffer, VkImage image, VkImageLayout layout,
                                      const VkClearColorValue* color, std::uint32_t count,
                                      const VkImageSubresourceRange* ranges) {
    auto& capture = *active_capture;
    if (count != 1 || ranges == nullptr || color == nullptr) {
        capture.malformed = true;
        return;
    }
    capture.Append({.command = Command::Clear,
                    .image = image,
                    .layout = layout,
                    .color = *color,
                    .range = ranges[0]});
}

struct Fixture {
    explicit Fixture(bool sync2) : previous{active_capture} {
        dispatch.vkCmdPipelineBarrier = RecordBarrier;
        dispatch.vkCmdPipelineBarrier2 = sync2 ? RecordBarrier2 : nullptr;
        dispatch.vkCmdClearColorImage = RecordClear;
        active_capture = &capture;
    }

    ~Fixture() {
        active_capture = previous;
    }

    Capture capture;
    Vulkan::vk::DeviceDispatch dispatch{};
    Vulkan::vk::CommandBuffer commands{FakeHandle<VkCommandBuffer>(1), dispatch};
    VkImage image{FakeHandle<VkImage>(2)};
    Capture* previous;
};

void CheckRange(const VkImageSubresourceRange& range) {
    CHECK(range.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(range.baseMipLevel == 0);
    CHECK(range.levelCount == 1);
    CHECK(range.baseArrayLayer == 0);
    CHECK(range.layerCount == 1);
}

void CheckBarrier(const VkImageMemoryBarrier2& barrier, VkImage image, VkImageLayout source,
                  VkImageLayout target) {
    CHECK(barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    CHECK(barrier.pNext == nullptr);
    CHECK(barrier.image == image);
    CHECK(barrier.oldLayout == source);
    CHECK(barrier.newLayout == target);
    CHECK(barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CheckRange(barrier.subresourceRange);
}

void CheckDispatch(const Fixture& fixture, bool sync2, unsigned barriers) {
    CHECK_FALSE(fixture.capture.malformed);
    CHECK(fixture.capture.sync2_calls == (sync2 ? barriers : 0));
    CHECK(fixture.capture.legacy_calls == (sync2 ? 0 : barriers));
}

constexpr VkAccessFlags2 PresentationAccess = VK_ACCESS_2_SHADER_READ_BIT |
                                             VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

} // namespace

TEST_CASE("Presentation clear is ordered between transfer and consumer barriers",
          "[video_core][present_transfer_sync]") {
    for (const bool sync2 : {false, true}) {
        CAPTURE(sync2);
        Fixture fixture{sync2};
        Vulkan::ClearColorImage(fixture.commands, fixture.image);
        CheckDispatch(fixture, sync2, 2);
        REQUIRE(fixture.capture.count == 3);
        const auto& before = fixture.capture.events[0];
        const auto& clear = fixture.capture.events[1];
        const auto& after = fixture.capture.events[2];
        REQUIRE(before.command == Command::Barrier);
        REQUIRE(clear.command == Command::Clear);
        REQUIRE(after.command == Command::Barrier);
        CheckBarrier(before.barrier, fixture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(before.barrier.srcAccessMask == 0);
        CHECK(before.barrier.dstStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(before.barrier.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CHECK(clear.image == fixture.image);
        CHECK(clear.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CheckRange(clear.range);
        for (const auto component : clear.color.uint32) {
            CHECK(component == 0);
        }
        CheckBarrier(after.barrier, fixture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_GENERAL);
        CHECK(after.barrier.srcStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(after.barrier.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CHECK(after.barrier.dstStageMask == Vulkan::vk::PIPELINE_STAGE_GRAPHICS_COMPUTE);
        CHECK(after.barrier.dstAccessMask == PresentationAccess);
    }
}

TEST_CASE("Presentation upload transitions cover transfer writes in both directions",
          "[video_core][present_transfer_sync]") {
    for (const bool sync2 : {false, true}) {
        CAPTURE(sync2);
        Fixture fixture{sync2};
        // These are the two production transitions surrounding UploadImage's copy.
        // The allocator, staging copy and scheduler are not simulated by this case.
        Vulkan::TransitionImageLayout(fixture.commands, fixture.image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_UNDEFINED);
        Vulkan::TransitionImageLayout(fixture.commands, fixture.image, VK_IMAGE_LAYOUT_GENERAL,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CheckDispatch(fixture, sync2, 2);
        REQUIRE(fixture.capture.count == 2);
        const auto& before = fixture.capture.events[0].barrier;
        const auto& after = fixture.capture.events[1].barrier;
        CheckBarrier(before, fixture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(before.srcStageMask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
        CHECK(before.srcAccessMask == 0);
        CHECK(before.dstStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(before.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CheckBarrier(after, fixture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_GENERAL);
        CHECK(after.srcStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(after.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CHECK(after.dstStageMask == Vulkan::vk::PIPELINE_STAGE_GRAPHICS_COMPUTE);
        CHECK(after.dstAccessMask == PresentationAccess);
    }
}

TEST_CASE("Presentation general layout dependencies retain their existing scopes",
          "[video_core][present_transfer_sync]") {
    for (const bool sync2 : {false, true}) {
        CAPTURE(sync2);
        Fixture fixture{sync2};
        Vulkan::TransitionImageLayout(fixture.commands, fixture.image, VK_IMAGE_LAYOUT_GENERAL);
        CheckDispatch(fixture, sync2, 1);
        REQUIRE(fixture.capture.count == 1);
        const auto& barrier = fixture.capture.events[0].barrier;
        CheckBarrier(barrier, fixture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        CHECK(barrier.srcStageMask == Vulkan::vk::PIPELINE_STAGE_GRAPHICS_COMPUTE);
        CHECK(barrier.dstStageMask == Vulkan::vk::PIPELINE_STAGE_GRAPHICS_COMPUTE);
        CHECK(barrier.srcAccessMask == PresentationAccess);
        CHECK(barrier.dstAccessMask == PresentationAccess);
    }
}
