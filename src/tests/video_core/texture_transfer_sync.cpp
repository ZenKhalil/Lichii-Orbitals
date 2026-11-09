// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/vk_texture_transfer.h"

namespace {

enum class Command { Barrier, BufferCopy, ImageCopy };

struct Event {
    Command command{};
    std::uint32_t count{};
    std::array<VkImageMemoryBarrier2, 2> barriers{};
    VkBuffer buffer{};
    VkImage source{};
    VkImage destination{};
    VkImageLayout source_layout{};
    VkImageLayout destination_layout{};
    std::array<VkBufferImageCopy, 2> buffer_regions{};
    std::array<VkImageCopy, 2> image_regions{};
};

struct Capture {
    std::array<Event, 3> events{};
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

VKAPI_ATTR void VKAPI_CALL RecordBarrier2(VkCommandBuffer, const VkDependencyInfo* info) {
    auto& capture = *active_capture;
    ++capture.sync2_calls;
    if (info->sType != VK_STRUCTURE_TYPE_DEPENDENCY_INFO || info->dependencyFlags != 0 ||
        info->memoryBarrierCount != 0 || info->bufferMemoryBarrierCount != 0 ||
        info->imageMemoryBarrierCount == 0 || info->imageMemoryBarrierCount > 2 ||
        info->pImageMemoryBarriers == nullptr) {
        capture.malformed = true;
        return;
    }
    Event event{.command = Command::Barrier, .count = info->imageMemoryBarrierCount};
    std::copy_n(info->pImageMemoryBarriers, event.count, event.barriers.begin());
    capture.Append(event);
}

VKAPI_ATTR void VKAPI_CALL RecordBarrier(VkCommandBuffer, VkPipelineStageFlags source_stage,
                                         VkPipelineStageFlags destination_stage,
                                         VkDependencyFlags flags, std::uint32_t memory_count,
                                         const VkMemoryBarrier*, std::uint32_t buffer_count,
                                         const VkBufferMemoryBarrier*, std::uint32_t image_count,
                                         const VkImageMemoryBarrier* images) {
    auto& capture = *active_capture;
    ++capture.legacy_calls;
    if (flags != 0 || memory_count != 0 || buffer_count != 0 || image_count == 0 ||
        image_count > 2 || images == nullptr) {
        capture.malformed = true;
        return;
    }
    Event event{.command = Command::Barrier, .count = image_count};
    for (std::uint32_t i = 0; i < image_count; ++i) {
        const auto& image = images[i];
        capture.malformed |= image.sType != VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        event.barriers[i] = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = image.pNext,
            .srcStageMask = source_stage,
            .srcAccessMask = image.srcAccessMask,
            .dstStageMask = destination_stage,
            .dstAccessMask = image.dstAccessMask,
            .oldLayout = image.oldLayout,
            .newLayout = image.newLayout,
            .srcQueueFamilyIndex = image.srcQueueFamilyIndex,
            .dstQueueFamilyIndex = image.dstQueueFamilyIndex,
            .image = image.image,
            .subresourceRange = image.subresourceRange,
        };
    }
    capture.Append(event);
}

VKAPI_ATTR void VKAPI_CALL RecordBufferCopy(VkCommandBuffer, VkBuffer buffer, VkImage image,
                                            VkImageLayout layout, std::uint32_t count,
                                            const VkBufferImageCopy* regions) {
    auto& capture = *active_capture;
    if (count == 0 || count > 2 || regions == nullptr) {
        capture.malformed = true;
        return;
    }
    Event event{.command = Command::BufferCopy,
                .count = count,
                .buffer = buffer,
                .destination = image,
                .destination_layout = layout};
    std::copy_n(regions, count, event.buffer_regions.begin());
    capture.Append(event);
}

VKAPI_ATTR void VKAPI_CALL RecordImageCopy(VkCommandBuffer, VkImage source,
                                           VkImageLayout source_layout, VkImage destination,
                                           VkImageLayout destination_layout, std::uint32_t count,
                                           const VkImageCopy* regions) {
    auto& capture = *active_capture;
    if (count == 0 || count > 2 || regions == nullptr) {
        capture.malformed = true;
        return;
    }
    Event event{.command = Command::ImageCopy,
                .count = count,
                .source = source,
                .destination = destination,
                .source_layout = source_layout,
                .destination_layout = destination_layout};
    std::copy_n(regions, count, event.image_regions.begin());
    capture.Append(event);
}

struct Fixture {
    explicit Fixture(bool sync2) : previous{active_capture} {
        dispatch.vkCmdPipelineBarrier = RecordBarrier;
        dispatch.vkCmdPipelineBarrier2 = sync2 ? RecordBarrier2 : nullptr;
        dispatch.vkCmdCopyBufferToImage = RecordBufferCopy;
        dispatch.vkCmdCopyImage = RecordImageCopy;
        active_capture = &capture;
    }

    ~Fixture() {
        active_capture = previous;
    }

    Capture capture;
    Vulkan::vk::DeviceDispatch dispatch{};
    Vulkan::vk::CommandBuffer commands{FakeHandle<VkCommandBuffer>(1), dispatch};
    VkBuffer buffer{FakeHandle<VkBuffer>(2)};
    VkImage source{FakeHandle<VkImage>(3)};
    VkImage destination{FakeHandle<VkImage>(4)};
    Capture* previous;
};

bool CoversStage(VkPipelineStageFlags2 mask, VkPipelineStageFlags2 stage) {
    if ((mask & stage) != 0 || (mask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0) {
        return true;
    }
    constexpr auto graphics =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
        VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
        VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
        VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    if ((stage & graphics) != 0 && (mask & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) != 0) {
        return true;
    }
    return (stage == VK_PIPELINE_STAGE_2_COPY_BIT || stage == VK_PIPELINE_STAGE_2_CLEAR_BIT) &&
           (mask & VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT) != 0;
}

struct AccessRole {
    const char* name;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 read;
    VkAccessFlags2 write;
};

// Resource roles, not a copy of the implementation's aggregate stage constant.
constexpr std::array Roles{
    AccessRole{"vertex shader", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
               VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"tessellation control", VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT,
               VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"tessellation evaluation", VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT,
               VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"geometry shader", VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT,
               VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"task shader", VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT,
               VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"mesh shader", VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_READ_BIT,
               VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"fragment shader", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"compute", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
               VK_ACCESS_2_SHADER_WRITE_BIT},
    AccessRole{"early depth load", VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
    AccessRole{"late depth store", VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
    AccessRole{"color attachment", VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
    AccessRole{"copy", VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT},
    AccessRole{"clear", VK_PIPELINE_STAGE_2_CLEAR_BIT, 0, VK_ACCESS_2_TRANSFER_WRITE_BIT},
};

void CheckRoles(const VkImageMemoryBarrier2& barrier, bool source) {
    for (const auto& role : Roles) {
        CAPTURE(role.name, source);
        CHECK(CoversStage(source ? barrier.srcStageMask : barrier.dstStageMask, role.stage));
        const auto access = source ? barrier.srcAccessMask : barrier.dstAccessMask;
        CHECK((access & role.write) == role.write);
        if (!source) {
            CHECK((access & role.read) == role.read);
        }
    }
}

void CheckBarrier(const VkImageMemoryBarrier2& barrier, VkImage image, VkImageLayout old_layout,
                  VkImageLayout new_layout, VkImageAspectFlags aspect, unsigned mip,
                  unsigned levels, unsigned layer, unsigned layers) {
    CHECK(barrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    CHECK(barrier.pNext == nullptr);
    CHECK(barrier.image == image);
    CHECK(barrier.oldLayout == old_layout);
    CHECK(barrier.newLayout == new_layout);
    CHECK(barrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(barrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED);
    CHECK(barrier.subresourceRange.aspectMask == aspect);
    CHECK(barrier.subresourceRange.baseMipLevel == mip);
    CHECK(barrier.subresourceRange.levelCount == levels);
    CHECK(barrier.subresourceRange.baseArrayLayer == layer);
    CHECK(barrier.subresourceRange.layerCount == layers);
}

void CheckLayers(const VkImageSubresourceLayers& actual, const VkImageSubresourceLayers& expected) {
    CHECK(actual.aspectMask == expected.aspectMask);
    CHECK(actual.mipLevel == expected.mipLevel);
    CHECK(actual.baseArrayLayer == expected.baseArrayLayer);
    CHECK(actual.layerCount == expected.layerCount);
}

void CheckOffset(const VkOffset3D& actual, const VkOffset3D& expected) {
    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.z == expected.z);
}

void CheckExtent(const VkExtent3D& actual, const VkExtent3D& expected) {
    CHECK(actual.width == expected.width);
    CHECK(actual.height == expected.height);
    CHECK(actual.depth == expected.depth);
}

void CheckSequence(const Fixture& fixture, bool sync2, Command copy, unsigned barriers) {
    REQUIRE_FALSE(fixture.capture.malformed);
    REQUIRE(fixture.capture.count == 3);
    CHECK(fixture.capture.sync2_calls == (sync2 ? 2 : 0));
    CHECK(fixture.capture.legacy_calls == (sync2 ? 0 : 2));
    CHECK(fixture.capture.events[0].command == Command::Barrier);
    CHECK(fixture.capture.events[1].command == copy);
    CHECK(fixture.capture.events[2].command == Command::Barrier);
    CHECK(fixture.capture.events[0].count == barriers);
    CHECK(fixture.capture.events[2].count == barriers);
}

constexpr std::array<VkImageAspectFlags, 4> Aspects{
    VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT,
    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT};

std::array<VkImageAspectFlags, 2> RegionAspects(VkImageAspectFlags aspects) {
    if (aspects == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
        return {VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT};
    }
    return {aspects, aspects};
}

} // namespace

TEST_CASE("Texture uploads order prior producers and publish to all image consumers",
          "[video_core][texture_transfer_sync]") {
    for (const bool sync2 : {false, true}) {
        for (const bool initialized : {false, true}) {
            for (const auto aspect : Aspects) {
                CAPTURE(sync2, initialized, aspect);
                Fixture fixture{sync2};
                const auto region_aspects = RegionAspects(aspect);
                const std::array copies{
                    VkBufferImageCopy{
                        0x120, 64, 32, {region_aspects[0], 1, 2, 2}, {2, 3, 0}, {8, 4, 1}},
                    VkBufferImageCopy{
                        0x920, 16, 8, {region_aspects[1], 3, 5, 1}, {1, 1, 0}, {4, 2, 1}}};
                Vulkan::TextureTransfer::RecordBufferToImageCopy(fixture.commands, fixture.buffer,
                                                                 fixture.destination, aspect,
                                                                 initialized, copies);
                CheckSequence(fixture, sync2, Command::BufferCopy, 1);
                const auto& before = fixture.capture.events[0].barriers[0];
                const auto& after = fixture.capture.events[2].barriers[0];
                CheckBarrier(before, fixture.destination,
                             initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, aspect, 1, 3, 2, 4);
                CheckRoles(before, true);
                CHECK(CoversStage(before.dstStageMask, VK_PIPELINE_STAGE_2_COPY_BIT));
                CHECK(before.dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
                CheckBarrier(after, fixture.destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL, aspect, 1, 3, 2, 4);
                CHECK(CoversStage(after.srcStageMask, VK_PIPELINE_STAGE_2_COPY_BIT));
                CHECK(after.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
                CheckRoles(after, false);
                const auto& copy = fixture.capture.events[1];
                CHECK(copy.buffer == fixture.buffer);
                CHECK(copy.destination == fixture.destination);
                CHECK(copy.destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                REQUIRE(copy.count == copies.size());
                for (std::size_t i = 0; i < copies.size(); ++i) {
                    CHECK(copy.buffer_regions[i].bufferOffset == copies[i].bufferOffset);
                    CHECK(copy.buffer_regions[i].bufferRowLength == copies[i].bufferRowLength);
                    CHECK(copy.buffer_regions[i].bufferImageHeight == copies[i].bufferImageHeight);
                    CheckLayers(copy.buffer_regions[i].imageSubresource,
                                copies[i].imageSubresource);
                    CheckOffset(copy.buffer_regions[i].imageOffset, copies[i].imageOffset);
                    CheckExtent(copy.buffer_regions[i].imageExtent, copies[i].imageExtent);
                }
            }
        }
    }
}

TEST_CASE("Texture copies restore both source and destination visibility with separate ranges",
          "[video_core][texture_transfer_sync]") {
    for (const bool sync2 : {false, true}) {
        for (const auto aspect : Aspects) {
            CAPTURE(sync2, aspect);
            Fixture fixture{sync2};
            const auto region_aspects = RegionAspects(aspect);
            const std::array copies{VkImageCopy{{region_aspects[0], 2, 3, 2},
                                                {2, 3, 0},
                                                {region_aspects[0], 1, 5, 2},
                                                {4, 5, 0},
                                                {4, 2, 1}},
                                    VkImageCopy{{region_aspects[1], 4, 6, 1},
                                                {0, 0, 0},
                                                {region_aspects[1], 3, 9, 1},
                                                {1, 1, 0},
                                                {2, 2, 1}}};
            Vulkan::TextureTransfer::RecordImageCopy(fixture.commands, fixture.destination,
                                                     fixture.source, aspect, copies);
            CheckSequence(fixture, sync2, Command::ImageCopy, 2);
            const auto& pre = fixture.capture.events[0].barriers;
            const auto& post = fixture.capture.events[2].barriers;
            CheckBarrier(pre[0], fixture.source, VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, aspect, 2, 3, 3, 4);
            CheckBarrier(pre[1], fixture.destination, VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, aspect, 1, 3, 5, 5);
            for (const auto& barrier : pre) {
                CheckRoles(barrier, true);
                CHECK(CoversStage(barrier.dstStageMask, VK_PIPELINE_STAGE_2_COPY_BIT));
            }
            CHECK(pre[0].dstAccessMask == VK_ACCESS_2_TRANSFER_READ_BIT);
            CHECK(pre[1].dstAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
            CheckBarrier(post[0], fixture.source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL, aspect, 2, 3, 3, 4);
            CheckBarrier(post[1], fixture.destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_GENERAL, aspect, 1, 3, 5, 5);
            CHECK(post[0].srcAccessMask == 0);
            CHECK(post[1].srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
            for (const auto& barrier : post) {
                CHECK(CoversStage(barrier.srcStageMask, VK_PIPELINE_STAGE_2_COPY_BIT));
                CheckRoles(barrier, false);
            }
            const auto& copy = fixture.capture.events[1];
            CHECK(copy.source == fixture.source);
            CHECK(copy.destination == fixture.destination);
            CHECK(copy.source_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            CHECK(copy.destination_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            REQUIRE(copy.count == copies.size());
            for (std::size_t i = 0; i < copies.size(); ++i) {
                CheckLayers(copy.image_regions[i].srcSubresource, copies[i].srcSubresource);
                CheckLayers(copy.image_regions[i].dstSubresource, copies[i].dstSubresource);
                CheckOffset(copy.image_regions[i].srcOffset, copies[i].srcOffset);
                CheckOffset(copy.image_regions[i].dstOffset, copies[i].dstOffset);
                CheckExtent(copy.image_regions[i].extent, copies[i].extent);
            }
        }
    }
}
