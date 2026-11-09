// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_buffer_transfer.h"

namespace {

enum class Command { Barrier, Copy };

struct Event {
    Command command{};
    VkMemoryBarrier2 dependency{};
    VkBuffer source{};
    VkBuffer destination{};
    std::uint32_t region_count{};
    std::array<VkBufferCopy, 2> regions{};
};

struct Capture {
    std::array<Event, 8> events{};
    std::size_t count{};
    unsigned legacy_calls{};
    unsigned sync2_calls{};
    bool malformed{};

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

// Fixed capture storage avoids allocation and test assertions inside wrapper calls.
VKAPI_ATTR void VKAPI_CALL RecordBarrier2(VkCommandBuffer, const VkDependencyInfo* info) {
    auto& capture = *active_capture;
    ++capture.sync2_calls;
    if (info->sType != VK_STRUCTURE_TYPE_DEPENDENCY_INFO || info->dependencyFlags != 0 ||
        info->memoryBarrierCount != 1 || info->pMemoryBarriers == nullptr ||
        info->bufferMemoryBarrierCount != 0 || info->imageMemoryBarrierCount != 0) {
        capture.malformed = true;
        return;
    }
    capture.Append({.command = Command::Barrier, .dependency = *info->pMemoryBarriers});
}

VKAPI_ATTR void VKAPI_CALL RecordBarrier(VkCommandBuffer, VkPipelineStageFlags source_stage,
                                         VkPipelineStageFlags destination_stage,
                                         VkDependencyFlags flags, std::uint32_t memory_count,
                                         const VkMemoryBarrier* memory, std::uint32_t buffer_count,
                                         const VkBufferMemoryBarrier*, std::uint32_t image_count,
                                         const VkImageMemoryBarrier*) {
    auto& capture = *active_capture;
    ++capture.legacy_calls;
    if (flags != 0 || memory_count != 1 || memory == nullptr || buffer_count != 0 ||
        image_count != 0) {
        capture.malformed = true;
        return;
    }
    capture.malformed |= memory->sType != VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    capture.Append({.command = Command::Barrier,
                    .dependency = {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                        .pNext = memory->pNext,
                        .srcStageMask = source_stage,
                        .srcAccessMask = memory->srcAccessMask,
                        .dstStageMask = destination_stage,
                        .dstAccessMask = memory->dstAccessMask,
                    }});
}

VKAPI_ATTR void VKAPI_CALL RecordCopy(VkCommandBuffer, VkBuffer source, VkBuffer destination,
                                      std::uint32_t count, const VkBufferCopy* regions) {
    auto& capture = *active_capture;
    if (count == 0 || count > 2 || regions == nullptr) {
        capture.malformed = true;
        return;
    }
    Event event{.command = Command::Copy,
                .source = source,
                .destination = destination,
                .region_count = count};
    std::copy_n(regions, count, event.regions.begin());
    capture.Append(event);
}

struct Fixture {
    explicit Fixture(bool sync2) : previous{active_capture} {
        dispatch.vkCmdPipelineBarrier = RecordBarrier;
        dispatch.vkCmdPipelineBarrier2 = sync2 ? RecordBarrier2 : nullptr;
        dispatch.vkCmdCopyBuffer = RecordCopy;
        active_capture = &capture;
    }

    ~Fixture() {
        active_capture = previous;
    }

    Capture capture;
    Vulkan::vk::DeviceDispatch dispatch{};
    Vulkan::vk::CommandBuffer commands{FakeHandle<VkCommandBuffer>(1), dispatch};
    VkBuffer source{FakeHandle<VkBuffer>(2)};
    VkBuffer destination{FakeHandle<VkBuffer>(3)};
    Capture* previous;
};

bool CoversCopy(VkPipelineStageFlags2 mask) {
    return (mask & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)) != 0;
}

void CheckDependency(const VkMemoryBarrier2& barrier) {
    CHECK(barrier.sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    CHECK(barrier.pNext == nullptr);
    CHECK(CoversCopy(barrier.srcStageMask));
    CHECK(CoversCopy(barrier.dstStageMask));
    CHECK(barrier.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CHECK(barrier.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // This upload-only dependency must not grow into a general graphics stall.
    constexpr auto non_transfer =
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    CHECK((barrier.srcStageMask & non_transfer) == 0);
    CHECK((barrier.dstStageMask & non_transfer) == 0);
}

void CheckCopy(const Event& event, const Fixture& fixture, std::span<const VkBufferCopy> regions) {
    CHECK(event.source == fixture.source);
    CHECK(event.destination == fixture.destination);
    REQUIRE(event.region_count == regions.size());
    for (std::size_t i = 0; i < regions.size(); ++i) {
        CHECK(event.regions[i].srcOffset == regions[i].srcOffset);
        CHECK(event.regions[i].dstOffset == regions[i].dstOffset);
        CHECK(event.regions[i].size == regions[i].size);
    }
}

void CheckPair(bool sync2, VkDeviceSize second_offset, bool barrier) {
    Fixture fixture{sync2};
    const std::array first{VkBufferCopy{0x100, 0x1c00, 0x200}, VkBufferCopy{0x400, 0x3000, 0x80}};
    const std::array second{VkBufferCopy{0x900, second_offset, 0x200},
                            VkBufferCopy{0xc00, 0x4000, 0x80}};
    Vulkan::BufferTransfer::RecordUploadCopy(fixture.commands, fixture.source, fixture.destination,
                                             first, barrier);
    Vulkan::BufferTransfer::RecordUploadCopy(fixture.commands, fixture.source, fixture.destination,
                                             second, barrier);
    REQUIRE_FALSE(fixture.capture.malformed);
    std::array<std::size_t, 2> copy_positions{};
    std::size_t copy_count{};
    for (std::size_t i = 0; i < fixture.capture.count; ++i) {
        const auto& event = fixture.capture.events[i];
        if (event.command == Command::Copy) {
            REQUIRE(copy_count < copy_positions.size());
            copy_positions[copy_count++] = i;
        } else {
            CheckDependency(event.dependency);
        }
    }
    REQUIRE(copy_count == 2);
    CheckCopy(fixture.capture.events[copy_positions[0]], fixture, first);
    CheckCopy(fixture.capture.events[copy_positions[1]], fixture, second);
    const auto expected_barriers = barrier ? 2U : 0U;
    CHECK(fixture.capture.legacy_calls == (sync2 ? 0U : expected_barriers));
    CHECK(fixture.capture.sync2_calls == (sync2 ? expected_barriers : 0U));
    CHECK(fixture.capture.count == 2 + expected_barriers);
    if (barrier) {
        // A terminal publication barrier cannot satisfy this between-copy dependency.
        const bool ordered = std::any_of(
            fixture.capture.events.begin() + copy_positions[0] + 1,
            fixture.capture.events.begin() + copy_positions[1], [](const Event& event) {
                return event.command == Command::Barrier &&
                       CoversCopy(event.dependency.srcStageMask) &&
                       CoversCopy(event.dependency.dstStageMask) &&
                       (event.dependency.srcAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0 &&
                       (event.dependency.dstAccessMask & VK_ACCESS_2_TRANSFER_WRITE_BIT) != 0;
            });
        CHECK(ordered);
        CHECK(copy_positions[0] == 1);
        CHECK(copy_positions[1] == 3);
    } else {
        CHECK(copy_positions[0] == 0);
        CHECK(copy_positions[1] == 1);
    }
}

} // namespace

TEST_CASE("Reordered uploads order exact and partially overlapping writes",
          "[video_core][buffer_upload_sync]") {
    for (const bool sync2 : {false, true}) {
        for (const VkDeviceSize offset : {0x1c00, 0x1d00}) {
            CAPTURE(sync2, offset);
            CheckPair(sync2, offset, true);
        }
    }
}

TEST_CASE("Reordered disjoint uploads preserve regions and command order",
          "[video_core][buffer_upload_sync]") {
    for (const bool sync2 : {false, true}) {
        CAPTURE(sync2);
        CheckPair(sync2, 0x1e00, true);
    }
}

TEST_CASE("Reordered upload barrier opt-out preserves caller synchronization contract",
          "[video_core][buffer_upload_sync]") {
    for (const bool sync2 : {false, true}) {
        CAPTURE(sync2);
        CheckPair(sync2, 0x1e00, false);
    }
}
