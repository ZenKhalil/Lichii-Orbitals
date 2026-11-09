// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/uniform_family_assessment.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
inline constexpr std::array ComputeUniformFormats{
    VK_FORMAT_R32_SFLOAT,          VK_FORMAT_R32_UINT,          VK_FORMAT_R32_SINT,
    VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_SINT,
    VK_FORMAT_R16_UINT};

// Numeric class is independent of component width. The prepared R16_UINT view
// still defines a two-byte texel; its fetched unsigned component widens to U32.
inline constexpr std::array ComputeUniformTypes{
    Shader::UniformTexelType::Float, Shader::UniformTexelType::Uint,
    Shader::UniformTexelType::Sint,  Shader::UniformTexelType::Float,
    Shader::UniformTexelType::Uint,  Shader::UniformTexelType::Sint,
    Shader::UniformTexelType::Uint};
static_assert(ComputeUniformFormats.size() == ComputeUniformTypes.size());

inline std::optional<Shader::UniformTexelType> ComputeUniformType(
    VkFormat format, const std::array<bool, ComputeUniformFormats.size()>& supported) noexcept {
    for (std::size_t i = 0; i < ComputeUniformFormats.size(); ++i) {
        if (format == ComputeUniformFormats[i] && supported[i]) {
            return ComputeUniformTypes[i];
        }
    }
    return std::nullopt;
}

// Only the uniform declaration is specialized. Other images and their atomics
// retain their instructions, types and bindings, checked through complete replay.
inline UniformFamilyReason ComputeUniformFetchReason(
    const Shader::IR::Program& program, std::uint32_t* offending_opcode = nullptr) {
    using namespace Shader;
    using Reason = UniformFamilyReason;
    if (offending_opcode) {
        *offending_opcode = ~std::uint32_t{};
    }
    const auto& info = program.info;
    if (program.stage != Stage::Compute) {
        return Reason::Stage;
    }
    if (info.is_task || info.is_mesh) {
        return Reason::TaskOrMesh;
    }
    if (info.texture_buffer_descriptors.size() != 1) {
        return Reason::DeclarationCount;
    }
    const auto& desc = info.texture_buffer_descriptors.front();
    if (desc.count != 1) {
        return Reason::DescriptorCount;
    }
    if (desc.handle_program != ~u32{}) {
        return Reason::ComputedHandle;
    }
    if (desc.has_secondary) {
        return Reason::SecondaryHandle;
    }
    bool fetch{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::GetSparseFromOp) {
                if (offending_opcode) {
                    *offending_opcode = static_cast<std::uint32_t>(inst.GetOpcode());
                }
                return Reason::Sparse;
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
                    if (offending_opcode) {
                        *offending_opcode = static_cast<std::uint32_t>(inst.GetOpcode());
                    }
                    return Reason::BufferSampleOperation;
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
                if (offending_opcode) {
                    *offending_opcode = static_cast<std::uint32_t>(inst.GetOpcode());
                }
                return Reason::DescriptorIndex;
            }
            fetch |= inst.GetOpcode() == IR::Opcode::ImageFetch;
        }
    }
    return fetch ? Reason::Eligible : Reason::MissingFetch;
}

inline bool IsSingleComputeUniformFetch(const Shader::IR::Program& program) {
    return ComputeUniformFetchReason(program) == UniformFamilyReason::Eligible;
}
} // namespace Vulkan
