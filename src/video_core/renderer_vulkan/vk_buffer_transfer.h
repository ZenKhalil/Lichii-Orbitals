// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan::BufferTransfer {

// Records a stream-buffer upload on the already selected upload command buffer.
// The caller retains resources, supplies valid copy regions, and establishes any
// external synchronization when barrier is false. Submission still publishes the
// completed uploads to graphics/compute consumers through the scheduler.
void RecordUploadCopy(vk::CommandBuffer upload_cmdbuf, VkBuffer src_buffer, VkBuffer dst_buffer,
                      std::span<const VkBufferCopy> copies, bool barrier);

} // namespace Vulkan::BufferTransfer
