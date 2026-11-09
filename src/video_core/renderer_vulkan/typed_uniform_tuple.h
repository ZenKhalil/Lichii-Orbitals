// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "video_core/renderer_vulkan/typed_uniform_compute.h"

namespace Vulkan {

inline constexpr std::size_t MaxUniformTupleSize = 4;

// Preparation only: this policy is not used by current pipeline selection.
// Exact formats determine support; only their numeric classes enter shader keys.
inline constexpr std::array UniformTupleFormats{
    VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_SINT,
    VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_UINT,
    VK_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R16_UINT, VK_FORMAT_R8_UNORM};
inline constexpr std::array UniformTupleTypes{
    Shader::UniformTexelType::Float, Shader::UniformTexelType::Uint,
    Shader::UniformTexelType::Sint, Shader::UniformTexelType::Float,
    Shader::UniformTexelType::Uint, Shader::UniformTexelType::Sint,
    Shader::UniformTexelType::Uint, Shader::UniformTexelType::Float};
static_assert(UniformTupleFormats.size() == UniformTupleTypes.size());

struct UniformTupleKey {
    std::array<Shader::UniformTexelType, MaxUniformTupleSize> types{};
    std::uint8_t count{};

    bool operator==(const UniformTupleKey&) const = default;

    // Includes length, so leading/trailing Float entries cannot alias another tuple.
    [[nodiscard]] std::uint32_t Encode() const noexcept {
        if (count == 0 || count > MaxUniformTupleSize) {
            return ~std::uint32_t{};
        }
        std::uint32_t value = count;
        for (std::size_t i = 0; i < count; ++i) {
            if (static_cast<std::uint32_t>(types[i]) >= 3) {
                return ~std::uint32_t{};
            }
            value = value * 3 + static_cast<std::uint32_t>(types[i]);
        }
        return value;
    }
};

inline std::optional<UniformTupleKey> MakeUniformTupleKey(
    std::span<const VkFormat> formats,
    const std::array<bool, UniformTupleFormats.size()>& supported) noexcept {
    if (formats.empty() || formats.size() > MaxUniformTupleSize) {
        return std::nullopt;
    }
    UniformTupleKey key;
    key.count = static_cast<std::uint8_t>(formats.size());
    for (std::size_t entry = 0; entry < formats.size(); ++entry) {
        bool found{};
        for (std::size_t format = 0; format < UniformTupleFormats.size(); ++format) {
            if (formats[entry] == UniformTupleFormats[format] && supported[format]) {
                key.types[entry] = UniformTupleTypes[format];
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return key;
}

// Validate normalized post-TexturePass indices in the uniform namespace. Other
// normalized image instructions remain untouched; unknown image operations fail closed.
inline UniformFamilyReason ComputeUniformTupleReason(const Shader::IR::Program& program) {
    using namespace Shader;
    using Reason = UniformFamilyReason;
    const auto& declarations = program.info.texture_buffer_descriptors;
    if (program.stage != Stage::Compute) {
        return Reason::Stage;
    }
    if (program.info.is_task || program.info.is_mesh) {
        return Reason::TaskOrMesh;
    }
    if (declarations.empty() || declarations.size() > MaxUniformTupleSize) {
        return Reason::DeclarationCount;
    }
    for (const auto& declaration : declarations) {
        if (declaration.count != 1) {
            return Reason::DescriptorCount;
        }
        if (declaration.handle_program != ~u32{}) {
            return Reason::ComputedHandle;
        }
        if (declaration.has_secondary) {
            return Reason::SecondaryHandle;
        }
    }
    std::array<bool, MaxUniformTupleSize> fetched{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            const auto opcode = inst.GetOpcode();
            if (static_cast<std::size_t>(opcode) >= IR::NUM_OPCODES) {
                return Reason::OtherImageOperation;
            }
            switch (opcode) {
            case IR::Opcode::GetSparseFromOp:
                return Reason::Sparse;
            case IR::Opcode::ImageFetch:
            case IR::Opcode::ImageQueryDimensions: {
                const auto flags = inst.Flags<IR::TextureInstInfo>();
                if (flags.type != TextureType::Buffer) {
                    break;
                }
                const u32 index = flags.descriptor_index;
                if (index >= declarations.size()) {
                    return Reason::DescriptorIndex;
                }
                fetched[index] |= opcode == IR::Opcode::ImageFetch;
                break;
            }
            case IR::Opcode::ImageSampleImplicitLod:
            case IR::Opcode::ImageSampleExplicitLod:
            case IR::Opcode::ImageSampleDrefImplicitLod:
            case IR::Opcode::ImageSampleDrefExplicitLod:
            case IR::Opcode::ImageGather:
            case IR::Opcode::ImageGatherDref:
            case IR::Opcode::ImageGradient:
            case IR::Opcode::ImageQueryLod:
                if (inst.Flags<IR::TextureInstInfo>().type == TextureType::Buffer) {
                    return Reason::BufferSampleOperation;
                }
                break;
            case IR::Opcode::ImageRead:
            case IR::Opcode::ImageWrite:
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
                // Separate storage-image/buffer namespace, not a uniform index.
                break;
            default:
                if (IR::NameOf(opcode).find("Image") != std::string_view::npos) {
                    return Reason::OtherImageOperation;
                }
                break;
            }
        }
    }
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        if (!fetched[i]) {
            return Reason::MissingFetch;
        }
    }
    return Reason::Eligible;
}

} // namespace Vulkan
