// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/single_buffer_store.h"
#include "video_core/renderer_vulkan/typed_uniform_compute.h"
#include "video_core/shader_environment.h"

namespace {
using namespace Shader;

// Synthetic input producer. TexturePass, collection, immutable capture/replay and
// emission are production code; this does not decode guest shader instructions.
class MixedInput final : public VideoCommon::GenericEnvironment {
public:
    MixedInput() {
        stage = Stage::Compute;
        workgroup_size = {1, 1, 1};
        texture_bound = 0;
        auto lowered = std::make_shared<VideoCommon::LoweredNativeCode>();
        lowered->source_size = 32;
        lowered->code.resize(16, 0);
        Ampere::AppendNativeAddressMap(lowered->code, std::array<u32, 2>{0, 1});
        InitializeNativeCode(std::move(lowered));
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        REQUIRE(bank == 0);
        REQUIRE((offset == 0x100 || offset == 0x104));
        const u32 value = offset == 0x100 ? 0x11 : 0x22;
        RecordComputeCbufValue((u64{bank} << 32) | offset, value);
        return value;
    }
    TextureType ReadTextureType(u32 handle) override {
        REQUIRE((handle == 0x11 || handle == 0x22));
        RecordComputeTextureType(handle, TextureType::Buffer);
        return TextureType::Buffer;
    }
    TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        REQUIRE((handle == 0x11 || handle == 0x22));
        RecordComputeTextureFormat(handle, TexturePixelFormat::R32_UINT);
        return TexturePixelFormat::R32_UINT;
    }
    bool IsTexturePixelFormatInteger(u32 handle) override {
        return ReadTexturePixelFormat(handle) == TexturePixelFormat::R32_UINT;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
};

struct MixedProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    IR::Inst* fetch{};

    explicit MixedProgram(Environment& environment) {
        program.stage = Stage::Compute;
        program.workgroup_size = environment.WorkgroupSize();
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        IR::IREmitter ir{block};
        IR::TextureInstInfo flags{};
        flags.type.Assign(TextureType::Buffer);
        const auto value =
            ir.ImageFetch(ir.Imm32(0x100), ir.Imm32(1), {}, ir.Imm32(0), IR::U32{}, flags);
        fetch = value.Inst();
        const auto integer = ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(value, 0)});
        flags.image_format.Assign(ImageFormat::R32_UINT);
        static_cast<void>(ir.ImageAtomicIAdd(ir.Imm32(0x104), ir.Imm32(2), integer, flags));
        HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        Optimization::TexturePass(environment, program, host);
        Optimization::CollectShaderInfoPass(environment, program);
    }

    std::vector<u32> Emit(std::optional<UniformTexelType> type = std::nullopt) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        RuntimeInfo runtime{};
        if (type) {
            runtime.uniform_texel_types = {*type};
        }
        runtime.enable_shader_loop_safety = true;
        runtime.enable_bloom_workaround = false;
        Backend::Bindings bindings{};
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

std::vector<std::vector<u32>> Ops(const std::vector<u32>& code, u32 opcode) {
    std::vector<std::vector<u32>> result;
    for (size_t at = 5; at < code.size();) {
        const auto count = code[at] >> 16;
        REQUIRE(count > 0);
        REQUIRE(at + count <= code.size());
        if ((code[at] & 0xffff) == opcode) {
            result.emplace_back(code.begin() + at + 1, code.begin() + at + count);
        }
        at += count;
    }
    return result;
}

void CheckAtomic(const std::vector<u32>& code) {
    const auto atomics = Ops(code, 234); // OpAtomicIAdd
    REQUIRE(atomics.size() == 1);
    REQUIRE(atomics.front().size() == 6);
    const auto integers = Ops(code, 21);
    CHECK(std::ranges::any_of(integers, [&](const auto& type) {
        return type[0] == atomics.front()[0] && type[1] == 32 && type[2] == 0;
    }));
    const auto pointers = Ops(code, 60); // OpImageTexelPointer
    REQUIRE(pointers.size() == 1);
    CHECK(atomics.front()[2] == pointers.front()[1]);
    const auto images = Ops(code, 25);
    CHECK(std::ranges::any_of(images, [&](const auto& image) {
        return image[1] == atomics.front()[0] && image[2] == 5 && image[6] == 2 &&
               image[7] == 33; // Buffer, storage, R32ui
    }));
}

void CheckFetch(const std::vector<u32>& code, UniformTexelType type) {
    const auto fetches = Ops(code, 95);
    REQUIRE(fetches.size() == 1);
    const auto& fetch = fetches.front();
    const auto vectors = Ops(code, 23);
    const auto vector =
        std::ranges::find_if(vectors, [&](const auto& value) { return value[0] == fetch[0]; });
    REQUIRE(vector != vectors.end());
    REQUIRE(vector->size() == 3);
    CHECK((*vector)[2] == 4);
    const auto scalar = (*vector)[1];
    const auto loads = Ops(code, 61);
    const auto load =
        std::ranges::find_if(loads, [&](const auto& value) { return value[1] == fetch[2]; });
    REQUIRE(load != loads.end());
    const auto images = Ops(code, 25);
    CHECK(std::ranges::any_of(images, [&](const auto& image) {
        return image[0] == (*load)[0] && image[1] == scalar && image[2] == 5 && image[6] == 1;
    }));
    if (type == UniformTexelType::Float) {
        const auto floats = Ops(code, 22);
        CHECK(std::ranges::any_of(
            floats, [&](const auto& value) { return value[0] == scalar && value[1] == 32; }));
    } else {
        const auto integers = Ops(code, 21);
        CHECK(std::ranges::any_of(integers, [&](const auto& value) {
            return value[0] == scalar && value[1] == 32 &&
                   value[2] == (type == UniformTexelType::Sint ? 1U : 0U);
        }));
        const auto floats = Ops(code, 22);
        const auto bitcasts = Ops(code, 124);
        CHECK(std::ranges::any_of(bitcasts, [&](const auto& value) {
            if (value[2] != fetch[1]) {
                return false;
            }
            const auto carrier = std::ranges::find_if(
                vectors, [&](const auto& entry) { return entry[0] == value[0] && entry[2] == 4; });
            return carrier != vectors.end() && std::ranges::any_of(floats, [&](const auto& entry) {
                       return entry[0] == (*carrier)[1] && entry[1] == 32;
                   });
        }));
    }
}

struct TemporaryRecord {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("orbitals-uniform-replay-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    ~TemporaryRecord() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace

TEST_CASE("Compute uniform eligibility retains unrelated image atomics",
          "[compute_uniform_family]") {
    MixedInput input;
    MixedProgram fixture{input};
    REQUIRE(fixture.program.info.uses_atomic_image_u32);
    REQUIRE(fixture.program.info.image_buffer_descriptors.size() == 1);
    REQUIRE(fixture.fetch->GetOpcode() == IR::Opcode::ImageFetch);
    CHECK(Vulkan::IsSingleComputeUniformFetch(fixture.program));
    std::uint32_t reused_opcode = static_cast<std::uint32_t>(IR::Opcode::ImageQueryLod);
    CHECK(Vulkan::ComputeUniformFetchReason(fixture.program, &reused_opcode) ==
          Vulkan::UniformFamilyReason::Eligible);
    CHECK(reused_opcode == ~std::uint32_t{});
    CHECK_FALSE(IsSingleTypelessBufferStore(fixture.program));
    for (u32 mutation = 0; mutation < 10; ++mutation) {
        MixedProgram changed{input};
        auto& desc = changed.program.info.texture_buffer_descriptors.front();
        switch (mutation) {
        case 0:
            changed.program.stage = Stage::Fragment;
            break;
        case 1:
            desc.count = 2;
            break;
        case 2:
            desc.handle_program = 0;
            break;
        case 3:
            desc.has_secondary = true;
            break;
        case 4:
            changed.program.info.texture_buffer_descriptors.push_back(desc);
            break;
        case 5:
            changed.fetch->Invalidate();
            break;
        case 6: {
            auto flags = changed.fetch->Flags<IR::TextureInstInfo>();
            flags.descriptor_index.Assign(1);
            changed.fetch->SetFlags(flags);
            break;
        }
        case 7: {
            IR::IREmitter ir{changed.block};
            static_cast<void>(ir.GetSparseFromOp(IR::Value{changed.fetch}));
            break;
        }
        case 8:
            changed.program.info.is_mesh = true;
            break;
        case 9: {
            IR::IREmitter ir{changed.block};
            const auto flags = changed.fetch->Flags<IR::TextureInstInfo>();
            const auto query = ir.ImageQueryLod(ir.Imm32(0), ir.Imm32(0), flags);
            query.Inst()->ReplaceOpcode(IR::Opcode::ImageQueryLod);
            break;
        }
        }
        CAPTURE(mutation);
        CHECK_FALSE(Vulkan::IsSingleComputeUniformFetch(changed.program));
        using Reason = Vulkan::UniformFamilyReason;
        constexpr std::array reasons{
            Reason::Stage, Reason::DescriptorCount, Reason::ComputedHandle,
            Reason::SecondaryHandle, Reason::DeclarationCount, Reason::MissingFetch,
            Reason::DescriptorIndex, Reason::Sparse, Reason::TaskOrMesh,
            Reason::BufferSampleOperation};
        std::uint32_t offending_opcode = 123;
        CHECK(Vulkan::ComputeUniformFetchReason(changed.program, &offending_opcode) ==
              reasons[mutation]);
        if (mutation == 6) {
            CHECK(offending_opcode == static_cast<std::uint32_t>(IR::Opcode::ImageFetch));
        } else if (mutation == 7) {
            CHECK(offending_opcode == static_cast<std::uint32_t>(IR::Opcode::GetSparseFromOp));
        } else if (mutation == 9) {
            CHECK(offending_opcode == static_cast<std::uint32_t>(IR::Opcode::ImageQueryLod));
        } else {
            CHECK(offending_opcode == ~std::uint32_t{});
        }
    }
}

TEST_CASE("Fresh cold and warm mixed uniform atomic replays preserve complete metadata",
          "[compute_uniform_family]") {
    MixedInput source;
    source.BeginComputeInputCapture();
    MixedProgram original{source};
    const auto expected_info = original.program.info;
    auto snapshot = VideoCommon::ComputeEnvironmentSnapshot::Capture(source);
    TemporaryRecord record;
    {
        std::ofstream output{record.path, std::ios::binary};
        REQUIRE(output.good());
        source.Serialize(output);
        REQUIRE(output.good());
    }
    VideoCommon::FileEnvironment disk;
    {
        std::ifstream input{record.path, std::ios::binary};
        disk.Deserialize(input);
        REQUIRE(input.good());
    }
    const auto warm = VideoCommon::ComputeEnvironmentSnapshot::CaptureDiskCompute(disk);
    const auto original_code = original.Emit();
    CheckFetch(original_code, UniformTexelType::Float);
    CheckAtomic(original_code);
    const Info emitted_info = original.program.info;
    for (auto type : {UniformTexelType::Float, UniformTexelType::Uint, UniformTexelType::Sint}) {
        auto cold_env = snapshot.CreateReplay();
        auto warm_env = warm.CreateReplay();
        MixedProgram cold{cold_env};
        MixedProgram loaded{warm_env};
        REQUIRE(cold.program.info == expected_info);
        REQUIRE(loaded.program.info == expected_info);
        REQUIRE(Vulkan::IsSingleComputeUniformFetch(cold.program));
        const auto cold_code = cold.Emit(type);
        const auto warm_code = loaded.Emit(type);
        CHECK(cold_code == warm_code);
        CheckAtomic(cold_code);
        CheckFetch(cold_code, type);
        CHECK(Ops(cold_code, 111).empty());
        CHECK(Ops(cold_code, 112).empty());
        CHECK(cold.program.info == emitted_info);
        CHECK(loaded.program.info == cold.program.info);
    }
}

TEST_CASE("Compute uniform host policy preserves exact bounded view formats",
          "[compute_uniform_family]") {
    std::array<bool, Vulkan::ComputeUniformFormats.size()> supported;
    supported.fill(true);
    const std::array types{
        UniformTexelType::Float, UniformTexelType::Uint, UniformTexelType::Sint,
        UniformTexelType::Float, UniformTexelType::Uint, UniformTexelType::Sint,
        UniformTexelType::Uint};
    REQUIRE(types.size() == supported.size());
    for (size_t i = 0; i < supported.size(); ++i) {
        CHECK(Vulkan::ComputeUniformType(Vulkan::ComputeUniformFormats[i], supported) ==
              types[i]);
        auto unavailable = supported;
        unavailable[i] = false;
        CHECK_FALSE(Vulkan::ComputeUniformType(Vulkan::ComputeUniformFormats[i], unavailable));
    }
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R8_UNORM, supported));
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_UNDEFINED, supported));
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R16_SINT, supported));
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R16_SFLOAT, supported));
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R16G16_UINT, supported));
}

TEST_CASE("R16 unsigned uniform selection preserves U32 fetch and unrelated image atomic",
          "[compute_uniform_family]") {
    std::array<bool, Vulkan::ComputeUniformFormats.size()> supported{};
    supported.back() = true;
    REQUIRE(Vulkan::ComputeUniformFormats.back() == VK_FORMAT_R16_UINT);
    const auto selected = Vulkan::ComputeUniformType(VK_FORMAT_R16_UINT, supported);
    REQUIRE(selected == UniformTexelType::Uint);
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R32_UINT, supported));

    MixedInput input;
    MixedProgram program{input};
    REQUIRE(Vulkan::IsSingleComputeUniformFetch(program.program));
    const auto code = program.Emit(*selected);
    CheckFetch(code, UniformTexelType::Uint);
    CheckAtomic(code);
    CHECK(Ops(code, 111).empty());
    CHECK(Ops(code, 112).empty());
    supported.back() = false;
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R16_UINT, supported));
}
