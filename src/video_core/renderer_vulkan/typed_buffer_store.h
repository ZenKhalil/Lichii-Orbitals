// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>

#include "shader_recompiler/runtime_info.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

inline constexpr std::array TypedBufferStoreFormats{
    VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};

inline constexpr bool SupportsTypedBufferStore(VkFormatFeatureFlags2 features) noexcept {
    constexpr VkFormatFeatureFlags2 required{VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                                             VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT};
    return (features & required) == required;
}

inline std::optional<Shader::ImageBufferStoreType> TypedBufferStoreType(
    VkFormat format, const std::array<bool, TypedBufferStoreFormats.size()>& supported) noexcept {
    using Type = Shader::ImageBufferStoreType;
    constexpr std::array types{Type::Uint, Type::Sint, Type::Float, Type::Float};
    for (size_t i = 0; i < TypedBufferStoreFormats.size(); ++i) {
        if (format == TypedBufferStoreFormats[i] && supported[i]) {
            return types[i];
        }
    }
    return std::nullopt;
}

} // namespace Vulkan
