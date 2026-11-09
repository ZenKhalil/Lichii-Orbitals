// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>

#include "shader_recompiler/exception.h"
#include "video_core/renderer_vulkan/typed_uniform_tuple.h"

namespace Vulkan {

inline constexpr size_t Uniform2MemberCount = 9;
using Uniform2Types = std::array<Shader::UniformTexelType, 2>;

inline bool ParseMultiUniform2(const char* value) noexcept {
    return value && std::string_view{value} == "1";
}

inline bool MultiUniform2Enabled() {
    static const bool enabled = ParseMultiUniform2(std::getenv("EDEN_NX2_MULTI_UNIFORM2"));
    return enabled;
}

inline Uniform2Types Uniform2TypesAt(size_t index) noexcept {
    return {static_cast<Shader::UniformTexelType>(index / 3),
            static_cast<Shader::UniformTexelType>(index % 3)};
}

inline std::optional<size_t> Uniform2Index(const Uniform2Types& types) noexcept {
    const auto first = static_cast<size_t>(types[0]);
    const auto second = static_cast<size_t>(types[1]);
    if (first >= 3 || second >= 3) {
        return std::nullopt;
    }
    return first * 3 + second;
}

inline UniformFamilyReason ComputeUniform2Reason(const Shader::IR::Program& program) {
    if (program.info.texture_buffer_descriptors.size() != 2) {
        return UniformFamilyReason::DeclarationCount;
    }
    // This experiment does not compose uniform and storage-texel specialization.
    // Ordinary images and their atomics are preserved by complete fresh replay.
    if (!program.info.image_buffer_descriptors.empty()) {
        return UniformFamilyReason::StorageImageDeclarations;
    }
    return ComputeUniformTupleReason(program);
}

inline std::optional<size_t> Uniform2FormatIndex(
    const std::array<VkFormat, 2>& formats,
    const std::array<bool, UniformTupleFormats.size()>& supported) noexcept {
    const auto key = MakeUniformTupleKey(formats, supported);
    if (!key) {
        return std::nullopt;
    }
    return Uniform2Index({key->types[0], key->types[1]});
}

// Fixed-size family assembly shared by the actual constructor path and its
// cleanup tests. Construction remains synchronous and publication happens last.
template <class Pipeline, class Construct>
std::unique_ptr<Pipeline> BuildUniform2Members(
    Construct&& construct, const std::array<bool, UniformTupleFormats.size()>& supported) {
    std::array<std::unique_ptr<Pipeline>, Uniform2MemberCount> members;
    for (size_t i = 0; i < members.size(); ++i) {
        members[i] = construct(i);
        if (!members[i] || !members[i]->IsBound() || !members[i]->IsBuildTaskDone()) {
            throw Shader::RuntimeError("Two-uniform compute member failed to build");
        }
    }
    auto base = std::move(members[0]);
    base->SetUniform2Family(std::move(members), supported);
    return base;
}

} // namespace Vulkan
