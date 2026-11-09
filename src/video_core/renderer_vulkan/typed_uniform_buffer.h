// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>

#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

inline constexpr std::array TypedUniformFormats{
    VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT};

inline std::optional<Shader::UniformTexelType> TypedUniformType(
    VkFormat format, const std::array<bool, 3>& supported) noexcept {
    for (size_t index = 0; index < TypedUniformFormats.size(); ++index) {
        if (format == TypedUniformFormats[index] && supported[index]) {
            return static_cast<Shader::UniformTexelType>(index);
        }
    }
    return std::nullopt;
}

// Deliberately one native VertexB declaration. Emitted stage 0 alone is not
// enough: VertexA and task/mesh programs can also occupy that renderer slot.
inline bool IsSingleVertexUniformFetch(const Shader::IR::Program& program) {
    const auto& info = program.info;
    if (program.stage != Shader::Stage::VertexB || info.is_task || info.is_mesh ||
        info.requires_layer_emulation || info.texture_buffer_descriptors.size() != 1 ||
        !info.image_buffer_descriptors.empty() || !info.texture_descriptors.empty() ||
        !info.image_descriptors.empty() || !info.handle_programs.empty()) {
        return false;
    }
    const auto& descriptor = info.texture_buffer_descriptors.front();
    if (descriptor.count != 1 || descriptor.handle_program != ~u32{}) {
        return false;
    }
    bool fetch{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            switch (inst.GetOpcode()) {
            case Shader::IR::Opcode::GetSparseFromOp:
            case Shader::IR::Opcode::ImageSampleImplicitLod:
            case Shader::IR::Opcode::ImageSampleExplicitLod:
            case Shader::IR::Opcode::ImageSampleDrefImplicitLod:
            case Shader::IR::Opcode::ImageSampleDrefExplicitLod:
            case Shader::IR::Opcode::ImageGather:
            case Shader::IR::Opcode::ImageGatherDref:
            case Shader::IR::Opcode::ImageGradient:
            case Shader::IR::Opcode::ImageQueryLod:
            case Shader::IR::Opcode::ImageRead:
            case Shader::IR::Opcode::ImageWrite:
                return false;
            default:
                break;
            }
            if (inst.GetOpcode() == Shader::IR::Opcode::ImageQueryDimensions) {
                const auto flags = inst.Flags<Shader::IR::TextureInstInfo>();
                if (flags.type != Shader::TextureType::Buffer || flags.descriptor_index != 0) {
                    return false;
                }
            }
            if (inst.GetOpcode() != Shader::IR::Opcode::ImageFetch) {
                continue;
            }
            const auto flags = inst.Flags<Shader::IR::TextureInstInfo>();
            if (flags.type != Shader::TextureType::Buffer || flags.descriptor_index != 0) {
                return false;
            }
            fetch = true;
        }
    }
    return fetch;
}

} // namespace Vulkan
