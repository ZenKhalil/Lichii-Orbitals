// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader {

// Deliberately excludes other texture/image declarations and coherent/byte-addressed modes.
inline bool IsSingleTypelessBufferStore(const IR::Program& program) {
    const auto& info = program.info;
    if (program.stage != Stage::Compute || info.is_task || info.is_mesh ||
        info.image_buffer_descriptors.size() != 1 || !info.texture_buffer_descriptors.empty() ||
        !info.texture_descriptors.empty() || !info.image_descriptors.empty() ||
        !info.handle_programs.empty() || info.uses_atomic_image_u32 ||
        info.requires_coherent_image_memory || info.uses_typeless_image_reads) {
        return false;
    }
    const auto& desc = info.image_buffer_descriptors.front();
    if (desc.format != ImageFormat::Typeless || desc.count != 1 || desc.is_read ||
        !desc.is_written || desc.handle_program != ~u32{}) {
        return false;
    }
    bool saw_store{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::ImageRead ||
                inst.GetOpcode() == IR::Opcode::GetAttribute ||
                inst.GetOpcode() == IR::Opcode::SetAttribute ||
                inst.GetOpcode() == IR::Opcode::ImageQueryDimensions ||
                inst.GetOpcode() == IR::Opcode::ImageFetch) {
                return false;
            }
            if (inst.GetOpcode() != IR::Opcode::ImageWrite) {
                continue;
            }
            const auto flags = inst.Flags<IR::TextureInstInfo>();
            if (flags.type != TextureType::Buffer || flags.image_format != ImageFormat::Typeless ||
                flags.descriptor_index != 0 || flags.image_byte_address != 0 ||
                flags.image_coherent != 0 || inst.Arg(2).Type() != IR::Type::U32x4) {
                return false;
            }
            saw_store = true;
        }
    }
    return saw_store;
}
} // namespace Shader
