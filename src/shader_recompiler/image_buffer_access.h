// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>
#include <vector>

#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader {

struct ImageBufferAccess {
    bool complete{true};
    bool target_read{};
    bool target_write{};
    bool target_atomic{};
    bool other_read_or_write{};
    std::vector<u32> other_atomic_counts;
};

// Classify normalized post-TexturePass instructions. Buffer and ordinary-image
// descriptor indices have separate namespaces. Unresolved/unknown image ops fail closed.
inline ImageBufferAccess ClassifyImageBufferAccess(const IR::Program& program, u32 target) {
    ImageBufferAccess result;
    result.other_atomic_counts.resize(program.info.image_descriptors.size());
    if (target >= program.info.image_buffer_descriptors.size()) {
        result.complete = false;
        return result;
    }
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            const auto opcode = inst.GetOpcode();
            bool read{};
            bool write{};
            bool atomic{};
            switch (opcode) {
            case IR::Opcode::ImageRead:
                read = true;
                break;
            case IR::Opcode::ImageWrite:
                write = true;
                break;
            case IR::Opcode::ImageAtomicIAdd32:
            case IR::Opcode::ImageAtomicSMin32:
            case IR::Opcode::ImageAtomicUMin32:
            case IR::Opcode::ImageAtomicSMax32:
            case IR::Opcode::ImageAtomicUMax32:
            case IR::Opcode::ImageAtomicInc32:
            case IR::Opcode::ImageAtomicDec32:
            case IR::Opcode::ImageAtomicAnd32:
            case IR::Opcode::ImageAtomicOr32:
            case IR::Opcode::ImageAtomicXor32:
            case IR::Opcode::ImageAtomicExchange32:
                read = write = atomic = true;
                break;
            default:
                if (static_cast<size_t>(opcode) >= IR::NUM_OPCODES ||
                    opcode == IR::Opcode::GetSparseFromOp ||
                    opcode == IR::Opcode::GetAttribute || opcode == IR::Opcode::SetAttribute ||
                    IR::NameOf(opcode).find("Image") != std::string_view::npos) {
                    result.complete = false;
                }
                continue;
            }
            const auto flags = inst.Flags<IR::TextureInstInfo>();
            const u32 index = flags.descriptor_index;
            if (flags.type == TextureType::Buffer) {
                if (index >= program.info.image_buffer_descriptors.size()) {
                    result.complete = false;
                    continue;
                }
                if (index != target) {
                    result.complete = false;
                    continue;
                }
                result.target_read |= read;
                result.target_write |= write;
                result.target_atomic |= atomic;
            } else {
                if (index >= program.info.image_descriptors.size()) {
                    result.complete = false;
                    continue;
                }
                const auto& descriptor = program.info.image_descriptors[index];
                if (descriptor.type != flags.type || descriptor.format != flags.image_format ||
                    flags.image_coherent != 0 || flags.image_byte_address != 0) {
                    result.complete = false;
                    continue;
                }
                if (atomic) {
                    // The existing emitter supports a single, non-array image only.
                    if (!inst.Arg(0).IsImmediate() || inst.Arg(0).U32() != 0) {
                        result.complete = false;
                    }
                    ++result.other_atomic_counts[index];
                } else {
                    result.other_read_or_write = true;
                }
            }
        }
    }
    return result;
}

} // namespace Shader
