// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan::TextureTransfer {

// Internal command recorders shared by the texture runtime and dispatch tests.
// Callers supply valid nonempty regions and keep the resources alive until GPU completion.
void RecordBufferToImageCopy(vk::CommandBuffer cmdbuf, VkBuffer src_buffer, VkImage image,
                             VkImageAspectFlags aspect_mask, bool is_initialized,
                             std::span<const VkBufferImageCopy> copies);

// Requires distinct images in GENERAL layout. Same-image copying needs a separate
// layout/overlap policy and is not supported by this recorder.
void RecordImageCopy(vk::CommandBuffer cmdbuf, VkImage dst_image, VkImage src_image,
                     VkImageAspectFlags aspect_mask, std::span<const VkImageCopy> copies);

} // namespace Vulkan::TextureTransfer
