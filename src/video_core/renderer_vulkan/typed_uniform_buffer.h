// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>

#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/uniform_family_assessment.h"
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
inline UniformFamilyReason VertexUniformFetchReason(
    const Shader::IR::Program& program, UniformFamilyAssessment* assessment = nullptr) {
    using Reason = UniformFamilyReason;
    const auto& info = program.info;
    if (assessment) {
        *assessment = {};
        assessment->observed = true;
        assessment->assessed_stage = 0;
        assessment->policy_evaluated = true;
        assessment->declaration_count = static_cast<u32>(info.texture_buffer_descriptors.size());
        if (info.texture_buffer_descriptors.size() == 1) {
            assessment->target_count = info.texture_buffer_descriptors.front().count;
        }
    }
    const auto finish = [&](Reason reason, u32 opcode = ~u32{}) {
        if (assessment) {
            assessment->reason = reason;
            assessment->policy_eligible = reason == Reason::Eligible;
            assessment->offending_opcode = opcode;
        }
        return reason;
    };
    if (program.stage != Shader::Stage::VertexB) {
        return finish(Reason::Stage);
    }
    if (info.is_task || info.is_mesh) {
        return finish(Reason::TaskOrMesh);
    }
    if (info.requires_layer_emulation) {
        return finish(Reason::LayerEmulation);
    }
    if (info.texture_buffer_descriptors.size() != 1) {
        return finish(Reason::DeclarationCount);
    }
    if (!info.image_buffer_descriptors.empty()) {
        return finish(Reason::StorageImageDeclarations);
    }
    if (!info.texture_descriptors.empty()) {
        return finish(Reason::TextureDeclarations);
    }
    if (!info.image_descriptors.empty()) {
        return finish(Reason::ImageDeclarations);
    }
    if (!info.handle_programs.empty()) {
        return finish(Reason::HandlePrograms);
    }
    const auto& descriptor = info.texture_buffer_descriptors.front();
    if (descriptor.count != 1) {
        return finish(Reason::DescriptorCount);
    }
    if (descriptor.handle_program != ~u32{}) {
        return finish(Reason::ComputedHandle);
    }
    if (descriptor.has_secondary) {
        return finish(Reason::SecondaryHandle);
    }
    bool fetch{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            const auto opcode = inst.GetOpcode();
            const auto encoded = static_cast<u32>(opcode);
            switch (opcode) {
            case Shader::IR::Opcode::GetSparseFromOp:
                return finish(Reason::Sparse, encoded);
            case Shader::IR::Opcode::ImageSampleImplicitLod:
            case Shader::IR::Opcode::ImageSampleExplicitLod:
            case Shader::IR::Opcode::ImageSampleDrefImplicitLod:
            case Shader::IR::Opcode::ImageSampleDrefExplicitLod:
            case Shader::IR::Opcode::ImageGather:
            case Shader::IR::Opcode::ImageGatherDref:
            case Shader::IR::Opcode::ImageGradient:
            case Shader::IR::Opcode::ImageQueryLod:
                return finish(Reason::BufferSampleOperation, encoded);
            case Shader::IR::Opcode::ImageRead:
            case Shader::IR::Opcode::ImageWrite:
                return finish(Reason::OtherImageOperation, encoded);
            default:
                break;
            }
            if (opcode != Shader::IR::Opcode::ImageQueryDimensions &&
                opcode != Shader::IR::Opcode::ImageFetch) {
                continue;
            }
            const auto flags = inst.Flags<Shader::IR::TextureInstInfo>();
            if (flags.type != Shader::TextureType::Buffer) {
                return finish(Reason::NonBufferOperation, encoded);
            }
            if (flags.descriptor_index != 0) {
                return finish(Reason::DescriptorIndex, encoded);
            }
            if (opcode == Shader::IR::Opcode::ImageFetch) {
                fetch = true;
            }
        }
    }
    return finish(fetch ? Reason::Eligible : Reason::MissingFetch);
}

inline bool IsSingleVertexUniformFetch(const Shader::IR::Program& program) {
    return VertexUniformFetchReason(program) == UniformFamilyReason::Eligible;
}

} // namespace Vulkan
