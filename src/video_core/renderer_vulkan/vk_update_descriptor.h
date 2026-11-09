// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

struct DescriptorAddress {
    VkDeviceAddress address;
    VkDeviceSize range;
    VkFormat format;
};

union DescriptorUpdateEntry {
    DescriptorUpdateEntry() = default;
    DescriptorUpdateEntry(VkDescriptorImageInfo image_) : image{image_} {}
    DescriptorUpdateEntry(VkDescriptorBufferInfo buffer_) : buffer{buffer_} {}
    DescriptorUpdateEntry(VkBufferView texel_buffer_) : texel_buffer{texel_buffer_} {}
    DescriptorUpdateEntry(DescriptorAddress address_) : address{address_} {}
    std::monostate empty{};
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
    VkBufferView texel_buffer;
    DescriptorAddress address;
};

class UpdateDescriptorQueue final {
    // This should be plenty for the vast majority of cases. Most desktop platforms only
    // provide up to 3 swapchain images.
    static constexpr size_t FRAMES_IN_FLIGHT = 8;

public:
    static constexpr size_t GUEST_FRAME_PAYLOAD_SIZE = 0x80000;
    static constexpr size_t COMPUTE_FRAME_PAYLOAD_SIZE = 0x20000;

    explicit UpdateDescriptorQueue(const Device& device_, size_t frame_payload_size_,
                                   bool supports_descriptor_buffer_ = false);
    ~UpdateDescriptorQueue();

    [[nodiscard]] bool UsesDescriptorBuffer() const noexcept {
        return use_descriptor_buffer;
    }

    void TickFrame();
    void Acquire(Scheduler& scheduler, size_t required_entries = 0,
                 bool use_descriptor_buffer_ = false);

    const DescriptorUpdateEntry* UpdateData() const noexcept {
        return upload_start;
    }

    size_t UpdateCount() const noexcept {
        return upload_start ? static_cast<size_t>(payload_cursor - upload_start) : 0;
    }

    struct PreparedTexelBuffer {
        VkBufferView view;
        VkDeviceSize offset;
        VkDeviceSize size;
        VkFormat format;
    };

    // Observe at most four ordered final host writes. The opt-in two-uniform
    // family enables two entries. Copies existing arguments, never creates a view.
    class TexelBufferObservation {
    public:
        static constexpr size_t Capacity = 4;

        struct Result {
            std::array<PreparedTexelBuffer, Capacity> bindings{};
            size_t count{};
        };

        void Reset() noexcept {
            count = 0;
            seen = 0;
            invalid = false;
        }

        bool Begin(std::span<const size_t> requested) noexcept {
            Reset();
            if (requested.empty() || requested.size() > Capacity) {
                return false;
            }
            for (size_t i = 0; i < requested.size(); ++i) {
                for (size_t j = 0; j < i; ++j) {
                    if (requested[i] == requested[j]) {
                        return false;
                    }
                }
                entries[i] = requested[i];
            }
            count = requested.size();
            return true;
        }

        void Record(size_t entry, const PreparedTexelBuffer& binding) noexcept {
            for (size_t i = 0; i < count; ++i) {
                if (entries[i] != entry) {
                    continue;
                }
                const auto bit = 1U << i;
                invalid |= (seen & bit) != 0;
                bindings[i] = binding;
                seen |= bit;
                return;
            }
        }

        [[nodiscard]] std::optional<Result> Take() noexcept {
            std::optional<Result> result;
            if (count != 0 && !invalid && seen == (1U << count) - 1) {
                result = Result{bindings, count};
            }
            Reset();
            return result;
        }

    private:
        std::array<size_t, Capacity> entries{};
        std::array<PreparedTexelBuffer, Capacity> bindings{};
        size_t count{};
        unsigned seen{};
        bool invalid{};
    };

    bool ObserveTexelBufferTuple(std::span<const size_t> entries) noexcept {
        return tuple_observation.Begin(entries);
    }

    [[nodiscard]] std::optional<TexelBufferObservation::Result> TakeTexelBufferTuple() noexcept {
        return tuple_observation.Take();
    }

    // Observe one existing host texel-buffer write without changing its payload or view.
    void ObserveTexelBufferFormat(size_t entry) noexcept {
        observed_texel_entry = entry;
        observed_texel_format.reset();
    }

    [[nodiscard]] std::optional<PreparedTexelBuffer> TakeTexelBufferBinding() noexcept {
        observed_texel_entry.reset();
        return std::exchange(observed_texel_format, std::nullopt);
    }

    void AddSampledImage(VkImageView image_view, VkSampler sampler) {
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    void AddImage(VkImageView image_view) {
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    void AddBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
        *(payload_cursor++) = VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range = size,
        };
    }

    void AddBuffer(VkBuffer buffer, VkDeviceAddress base_address, VkDeviceSize offset,
                   VkDeviceSize size) {
        if (!use_descriptor_buffer) {
            AddBuffer(buffer, offset, size);
            return;
        }
        *(payload_cursor++) = DescriptorAddress{
            .address = base_address == 0 ? 0 : base_address + offset,
            .range = base_address == 0 ? VK_WHOLE_SIZE : size,
            .format = VK_FORMAT_UNDEFINED,
        };
    }

    void AddTexelBuffer(VkBufferView texel_buffer) {
        *(payload_cursor++) = texel_buffer;
    }

    void AddTexelBuffer(VkBufferView texel_buffer, VkDeviceAddress base_address,
                        VkDeviceSize offset, VkDeviceSize size, VkFormat format) {
        if (observed_texel_entry && *observed_texel_entry == UpdateCount()) {
            observed_texel_format = PreparedTexelBuffer{texel_buffer, offset, size, format};
        }
        tuple_observation.Record(UpdateCount(), {texel_buffer, offset, size, format});
        if (!use_descriptor_buffer) {
            AddTexelBuffer(texel_buffer);
            return;
        }
        *(payload_cursor++) = DescriptorAddress{
            .address = base_address == 0 ? 0 : base_address + offset,
            .range = base_address == 0 ? VK_WHOLE_SIZE : size,
            .format = format,
        };
    }

private:
    const Device& device;
    size_t frame_payload_size;
    const size_t maximum_frame_payload_size;
    const bool supports_descriptor_buffer;
    bool use_descriptor_buffer{false};
    std::optional<size_t> observed_texel_entry;
    std::optional<PreparedTexelBuffer> observed_texel_format;
    TexelBufferObservation tuple_observation;
    size_t frame_index{0};
    DescriptorUpdateEntry* payload_cursor = nullptr;
    DescriptorUpdateEntry* payload_start = nullptr;
    const DescriptorUpdateEntry* upload_start = nullptr;
    std::unique_ptr<DescriptorUpdateEntry[]> payload;
    bool growth_disabled{};
    bool fallback_reported{};
};

// TODO: should these be separate classes instead?
using GuestDescriptorQueue = UpdateDescriptorQueue;
using ComputePassDescriptorQueue = UpdateDescriptorQueue;

} // namespace Vulkan
