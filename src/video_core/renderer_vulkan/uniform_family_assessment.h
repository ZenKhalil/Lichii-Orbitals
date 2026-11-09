// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace Vulkan {

enum class UniformFamilyReason : std::uint32_t {
    Unknown,
    Eligible,
    Stage,
    TaskOrMesh,
    LayerEmulation,
    DeclarationCount,
    StorageImageDeclarations,
    TextureDeclarations,
    ImageDeclarations,
    HandlePrograms,
    DescriptorCount,
    ComputedHandle,
    SecondaryHandle,
    Sparse,
    BufferSampleOperation,
    DescriptorIndex,
    MissingFetch,
    OtherImageOperation,
    NonBufferOperation,
    VertexA,
    OtherStageUniform,
    MissingCapture,
    NotNative,
    FormatUnavailable,
    SnapshotUnavailable,
    OtherFamilySelected,
};

// Diagnostic metadata only. Immutable once its pipeline is published; no guest
// pointers, compiler pools or driver objects are retained. A reason is the first
// failed check, not an exhaustive list of incompatibilities.
struct UniformFamilyAssessment {
    UniformFamilyReason reason{};
    // Emitted pipeline stage index, independent of the descriptor row stage.
    std::uint32_t assessed_stage{~std::uint32_t{}};
    std::uint32_t declaration_count{};
    std::uint32_t target_count{};
    std::uint32_t offending_opcode{~std::uint32_t{}};
    std::uint32_t supported_format_mask{};
    bool observed{};
    bool policy_evaluated{};
    bool policy_eligible{};
    bool features_queried{};
    bool snapshot_available{};
    bool family_built{};
};

constexpr const char* UniformFamilyReasonName(UniformFamilyReason reason) noexcept {
    switch (reason) {
    case UniformFamilyReason::Unknown:
        return "Unknown";
    case UniformFamilyReason::Eligible:
        return "Eligible";
    case UniformFamilyReason::Stage:
        return "Stage";
    case UniformFamilyReason::TaskOrMesh:
        return "TaskOrMesh";
    case UniformFamilyReason::LayerEmulation:
        return "LayerEmulation";
    case UniformFamilyReason::DeclarationCount:
        return "DeclarationCount";
    case UniformFamilyReason::StorageImageDeclarations:
        return "StorageImageDeclarations";
    case UniformFamilyReason::TextureDeclarations:
        return "TextureDeclarations";
    case UniformFamilyReason::ImageDeclarations:
        return "ImageDeclarations";
    case UniformFamilyReason::HandlePrograms:
        return "HandlePrograms";
    case UniformFamilyReason::DescriptorCount:
        return "DescriptorCount";
    case UniformFamilyReason::ComputedHandle:
        return "ComputedHandle";
    case UniformFamilyReason::SecondaryHandle:
        return "SecondaryHandle";
    case UniformFamilyReason::Sparse:
        return "Sparse";
    case UniformFamilyReason::BufferSampleOperation:
        return "BufferSampleOperation";
    case UniformFamilyReason::DescriptorIndex:
        return "DescriptorIndex";
    case UniformFamilyReason::MissingFetch:
        return "MissingFetch";
    case UniformFamilyReason::OtherImageOperation:
        return "OtherImageOperation";
    case UniformFamilyReason::NonBufferOperation:
        return "NonBufferOperation";
    case UniformFamilyReason::VertexA:
        return "VertexA";
    case UniformFamilyReason::OtherStageUniform:
        return "OtherStageUniform";
    case UniformFamilyReason::MissingCapture:
        return "MissingCapture";
    case UniformFamilyReason::NotNative:
        return "NotNative";
    case UniformFamilyReason::FormatUnavailable:
        return "FormatUnavailable";
    case UniformFamilyReason::SnapshotUnavailable:
        return "SnapshotUnavailable";
    case UniformFamilyReason::OtherFamilySelected:
        return "OtherFamilySelected";
    }
    return "Unknown";
}

} // namespace Vulkan
