// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
inline constexpr std::array ComputeUniformFormats{
    VK_FORMAT_R32_SFLOAT,          VK_FORMAT_R32_UINT,          VK_FORMAT_R32_SINT,
    VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SINT};

inline std::optional<Shader::UniformTexelType> ComputeUniformType(
    VkFormat format, const std::array<bool, ComputeUniformFormats.size()>& supported) noexcept {
    for (std::size_t i = 0; i < ComputeUniformFormats.size(); ++i) {
        if (format == ComputeUniformFormats[i] && supported[i]) {
            return static_cast<Shader::UniformTexelType>(i % 3);
        }
    }
    return std::nullopt;
}

// Only the uniform declaration is specialized. Other images and their atomics
// retain their instructions, types and bindings, checked through complete replay.
inline bool IsSingleComputeUniformFetch(const Shader::IR::Program& program) {
    using namespace Shader;
    const auto& info = program.info;
    if (program.stage != Stage::Compute || info.is_task || info.is_mesh ||
        info.texture_buffer_descriptors.size() != 1) {
        return false;
    }
    const auto& desc = info.texture_buffer_descriptors.front();
    if (desc.count != 1 || desc.handle_program != ~u32{} || desc.has_secondary) {
        return false;
    }
    bool fetch{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::GetSparseFromOp) {
                return false;
            }
            switch (inst.GetOpcode()) {
            case IR::Opcode::ImageSampleImplicitLod:
            case IR::Opcode::ImageSampleExplicitLod:
            case IR::Opcode::ImageSampleDrefImplicitLod:
            case IR::Opcode::ImageSampleDrefExplicitLod:
            case IR::Opcode::ImageGather:
            case IR::Opcode::ImageGatherDref:
            case IR::Opcode::ImageGradient:
            case IR::Opcode::ImageQueryLod:
                if (inst.Flags<IR::TextureInstInfo>().type == TextureType::Buffer) {
                    return false;
                }
                break;
            default:
                break;
            }
            if (inst.GetOpcode() != IR::Opcode::ImageFetch &&
                inst.GetOpcode() != IR::Opcode::ImageQueryDimensions) {
                continue;
            }
            const auto flags = inst.Flags<IR::TextureInstInfo>();
            if (flags.type != TextureType::Buffer) {
                continue;
            }
            if (flags.descriptor_index != 0) {
                return false;
            }
            fetch |= inst.GetOpcode() == IR::Opcode::ImageFetch;
        }
    }
    return fetch;
}
} // namespace Vulkan
