// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/image_buffer_access.h"

namespace Shader {

// One write-only buffer may coexist with unchanged, explicitly mapped non-buffer
// image atomics. Uniform/sample declarations and target atomic/coherent modes remain excluded.
inline bool IsSingleTypelessBufferStore(const IR::Program& program) {
    const auto& info = program.info;
    if (program.stage != Stage::Compute || info.is_task || info.is_mesh ||
        info.image_buffer_descriptors.size() != 1 || !info.texture_buffer_descriptors.empty() ||
        !info.texture_descriptors.empty() || !info.handle_programs.empty() ||
        info.requires_coherent_image_memory || info.uses_typeless_image_reads) {
        return false;
    }
    const auto& desc = info.image_buffer_descriptors.front();
    if (desc.format != ImageFormat::Typeless || desc.count != 1 || desc.is_read ||
        !desc.is_written || desc.handle_program != ~u32{}) {
        return false;
    }
    const auto access = ClassifyImageBufferAccess(program, 0);
    if (!access.complete || access.target_read || access.target_atomic || !access.target_write ||
        access.other_read_or_write) {
        return false;
    }
    bool other_atomic{};
    for (size_t index = 0; index < info.image_descriptors.size(); ++index) {
        const auto& image = info.image_descriptors[index];
        if (image.type == TextureType::Buffer || image.format != ImageFormat::R32_UINT ||
            image.count != 1 || image.handle_program != ~u32{} || !image.is_read ||
            !image.is_written || !image.is_integer || image.is_byte_addressed ||
            access.other_atomic_counts[index] == 0) {
            return false;
        }
        other_atomic = true;
    }
    if (info.uses_atomic_image_u32 != other_atomic) {
        return false;
    }
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            // These known operations are not implemented by the SPIR-V backend.
            if (inst.GetOpcode() == IR::Opcode::ImageAtomicInc32 ||
                inst.GetOpcode() == IR::Opcode::ImageAtomicDec32) {
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
        }
    }
    return true;
}

} // namespace Shader
