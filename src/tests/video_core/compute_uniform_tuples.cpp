// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "video_core/renderer_vulkan/compute_uniform2.h"
#include "video_core/renderer_vulkan/typed_uniform_tuple.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_environment.h"

namespace {
using namespace Shader;

// Synthetic IR producer: actual TexturePass, collection, serializer, immutable
// replays and SPIR-V emitter. This fixture does not decode guest instructions.
class TupleInput final : public VideoCommon::GenericEnvironment {
public:
    TupleInput() {
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
        REQUIRE((offset >= 0x100 && offset <= 0x110 && offset % 4 == 0));
        const u32 value = 0x11 + (offset - 0x100) / 4;
        RecordComputeCbufValue((u64{bank} << 32) | offset, value);
        return value;
    }

    TextureType ReadTextureType(u32 handle) override {
        REQUIRE((handle >= 0x11 && handle <= 0x15));
        const auto type = handle == 0x15 ? TextureType::Color2D : TextureType::Buffer;
        RecordComputeTextureType(handle, type);
        return type;
    }

    TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        REQUIRE((handle >= 0x11 && handle <= 0x15));
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

struct TupleProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    std::vector<IR::Inst*> fetches;

    TupleProgram(Environment& environment, u32 count) {
        program.stage = Stage::Compute;
        program.workgroup_size = environment.WorkgroupSize();
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        program.info.storage_buffers_descriptors = {{0, 0, 1, true}};
        IR::IREmitter ir{block};
        IR::TextureInstInfo flags{};
        flags.type.Assign(TextureType::Buffer);
        for (u32 entry = 0; entry < count; ++entry) {
            const auto fetched = ir.ImageFetch(ir.Imm32(0x100 + entry * 4), ir.Imm32(entry + 1),
                                               {}, ir.Imm32(0), IR::U32{}, flags);
            fetches.push_back(fetched.Inst());
            for (u32 component = 0; component < 4; ++component) {
                const auto value =
                    ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(fetched, component)});
                block.AppendNewInst(IR::Opcode::WriteStorage32,
                                    {ir.Imm32(0), ir.Imm32(entry * 16 + component * 4), value});
            }
        }
        flags.type.Assign(TextureType::Color2D);
        flags.image_format.Assign(ImageFormat::R32_UINT);
        static_cast<void>(ir.ImageAtomicIAdd(
            ir.Imm32(0x110), ir.CompositeConstruct(ir.Imm32(2), ir.Imm32(3)), ir.Imm32(7), flags));
        HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        Optimization::TexturePass(environment, program, host);
        Optimization::CollectShaderInfoPass(environment, program);
    }

    std::vector<u32> Emit(std::span<const UniformTexelType> types) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        profile.unified_descriptor_binding = true;
        RuntimeInfo runtime{};
        runtime.uniform_texel_types.assign(types.begin(), types.end());
        runtime.enable_shader_loop_safety = true;
        runtime.enable_bloom_workaround = false;
        Backend::Bindings bindings{};
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

using Instructions = std::vector<std::vector<u32>>;

Instructions Ops(const std::vector<u32>& code, u32 opcode) {
    Instructions result;
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

bool IsCarrier(const std::vector<u32>& code, u32 value, u32 fetch, u32 component, u32 depth = 0) {
    if (depth > 8) {
        return false;
    }
    for (const auto& extract : Ops(code, 81)) { // OpCompositeExtract
        if (extract.size() == 4 && extract[1] == value && extract[3] == component) {
            if (extract[2] == fetch) {
                return true;
            }
            for (const auto& cast : Ops(code, 124)) {
                if (cast.size() == 3 && cast[1] == extract[2] && cast[2] == fetch) {
                    return true;
                }
            }
        }
    }
    for (const auto& cast : Ops(code, 124)) {
        if (cast.size() == 3 && cast[1] == value &&
            IsCarrier(code, cast[2], fetch, component, depth + 1)) {
            return true;
        }
    }
    return false;
}

bool ConstantEquals(const std::vector<u32>& code, u32 id, u32 value) {
    return std::ranges::any_of(Ops(code, 43), [&](const auto& op) {
        return op.size() == 3 && op[1] == id && op[2] == value;
    });
}

bool HasDecoration(const std::vector<u32>& code, u32 id, u32 decoration, u32 value) {
    return std::ranges::any_of(Ops(code, 71), [&](const auto& op) {
        return op.size() == 3 && op[0] == id && op[1] == decoration && op[2] == value;
    });
}

bool IsStoragePointer(const std::vector<u32>& code, u32 pointer, u32 word_offset) {
    // Actual WriteStorage32 emits struct member zero + a constant U32-array index.
    for (const auto& access : Ops(code, 65)) {
        if (access.size() != 5 || access[1] != pointer ||
            !ConstantEquals(code, access[3], 0) || !ConstantEquals(code, access[4], word_offset)) {
            continue;
        }
        for (const auto& variable : Ops(code, 59)) {
            if (variable.size() < 3 || variable[1] != access[2] || variable[2] != 12 ||
                !HasDecoration(code, variable[1], 33, 0)) {
                continue;
            }
            for (const auto& pointer_type : Ops(code, 32)) {
                if (pointer_type.size() != 3 || pointer_type[0] != variable[0] ||
                    pointer_type[1] != 12) {
                    continue;
                }
                for (const auto& structure : Ops(code, 30)) {
                    if (structure.size() != 2 || structure[0] != pointer_type[2] ||
                        !HasDecoration(code, structure[1], 6, 4)) {
                        continue;
                    }
                    const auto offsets = Ops(code, 72);
                    if (std::ranges::any_of(offsets, [&](const auto& op) {
                            return op.size() == 4 && op[0] == structure[0] && op[1] == 0 &&
                                   op[2] == 35 && op[3] == 0;
                        })) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void CheckTuple(const std::vector<u32>& code, std::span<const UniformTexelType> types) {
    const auto fetches = Ops(code, 95);
    REQUIRE(fetches.size() == types.size());
    const auto vectors = Ops(code, 23);
    const auto loads = Ops(code, 61);
    const auto images = Ops(code, 25);
    const auto stores = Ops(code, 62);
    const auto decorations = Ops(code, 71);
    for (size_t i = 0; i < fetches.size(); ++i) {
        const auto& fetch = fetches[i];
        REQUIRE(fetch.size() >= 4);
        const auto vector = std::ranges::find_if(vectors, [&](const auto& op) {
            return op[0] == fetch[0];
        });
        REQUIRE(vector != vectors.end());
        REQUIRE(vector->size() == 3);
        CHECK((*vector)[2] == 4);
        const auto scalar = (*vector)[1];
        const auto load = std::ranges::find_if(loads, [&](const auto& op) {
            return op[1] == fetch[2];
        });
        REQUIRE(load != loads.end());
        CHECK(std::ranges::any_of(images, [&](const auto& op) {
            return op[0] == (*load)[0] && op[1] == scalar && op[2] == 5 && op[6] == 1;
        }));
        // SSBO occupies binding zero; uniform declaration order must be preserved.
        CHECK(std::ranges::any_of(decorations, [&](const auto& op) {
            return op.size() == 3 && op[0] == (*load)[2] && op[1] == 33 && op[2] == i + 1;
        }));
        if (types[i] == UniformTexelType::Float) {
            CHECK(std::ranges::any_of(Ops(code, 22), [&](const auto& op) {
                return op[0] == scalar && op[1] == 32;
            }));
        } else {
            CHECK(std::ranges::any_of(Ops(code, 21), [&](const auto& op) {
                return op[0] == scalar && op[1] == 32 &&
                       op[2] == (types[i] == UniformTexelType::Sint ? 1U : 0U);
            }));
        }
        for (u32 component = 0; component < 4; ++component) {
            CHECK(std::ranges::any_of(stores, [&](const auto& op) {
                return op.size() == 2 &&
                       IsStoragePointer(code, op[0], static_cast<u32>(i) * 4 + component) &&
                       IsCarrier(code, op[1], fetch[1], component);
            }));
        }
    }
    const auto atomics = Ops(code, 234);
    REQUIRE(atomics.size() == 1);
    REQUIRE(atomics.front().size() == 6);
    const auto pointers = Ops(code, 60);
    REQUIRE(pointers.size() == 1);
    REQUIRE(pointers.front().size() == 5);
    CHECK(atomics.front()[2] == pointers.front()[1]);
    const auto variables = Ops(code, 59);
    const auto variable = std::ranges::find_if(variables, [&](const auto& op) {
        return op[1] == pointers.front()[2];
    });
    REQUIRE(variable != variables.end());
    CHECK((*variable)[2] == 0); // UniformConstant image variable.
    CHECK(HasDecoration(code, (*variable)[1], 33, static_cast<u32>(types.size()) + 1));
    const auto pointer_types = Ops(code, 32);
    const auto pointer_type = std::ranges::find_if(pointer_types, [&](const auto& op) {
        return op[0] == (*variable)[0];
    });
    REQUIRE(pointer_type != pointer_types.end());
    CHECK(std::ranges::any_of(images, [&](const auto& op) {
        return op[0] == (*pointer_type)[2] && op[1] == atomics.front()[0] &&
               op[2] == 1 && op[6] == 2 && op[7] == 33; // Same 2D R32ui atomic resource.
    }));
    CHECK(std::ranges::any_of(Ops(code, 21), [&](const auto& op) {
        return op[0] == atomics.front()[0] && op[1] == 32 && op[2] == 0;
    }));
    CHECK(ConstantEquals(code, atomics.front()[3], 1)); // Device scope.
    CHECK(ConstantEquals(code, atomics.front()[4], 0)); // Relaxed semantics.
    CHECK(ConstantEquals(code, atomics.front()[5], 7)); // Original IAdd operand.
    CHECK(ConstantEquals(code, pointers.front()[4], 0)); // Sample index.
    bool coordinate_matches{};
    for (const u32 opcode : {44U, 80U}) {
        for (const auto& composite : Ops(code, opcode)) {
            coordinate_matches |= composite.size() == 4 && composite[1] == pointers.front()[3] &&
                                  ConstantEquals(code, composite[2], 2) &&
                                  ConstantEquals(code, composite[3], 3);
        }
    }
    CHECK(coordinate_matches);
    CHECK(Ops(code, 111).empty()); // No numeric float/int conversion replaces carrier bitcasts.
    CHECK(Ops(code, 112).empty());
}

struct TemporaryRecord {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("orbitals-tuple-replay-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    ~TemporaryRecord() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace

TEST_CASE("Uniform tuple policy validates every normalized declaration", "[compute_uniform_tuple]") {
    using Reason = Vulkan::UniformFamilyReason;
    TupleInput input;
    for (u32 count = 1; count <= 4; ++count) {
        TupleProgram original{input, count};
        REQUIRE(original.program.info.texture_buffer_descriptors.size() == count);
        CHECK(Vulkan::ComputeUniformTupleReason(original.program) == Reason::Eligible);
        CHECK(Vulkan::IsSingleComputeUniformFetch(original.program) == (count == 1));
        for (u32 index = 0; index < count; ++index) {
            for (u32 mutation = 0; mutation < 8; ++mutation) {
                TupleProgram changed{input, count};
                auto& desc = changed.program.info.texture_buffer_descriptors[index];
                auto* fetch = changed.fetches[index];
                switch (mutation) {
                case 0:
                    desc.count = 2;
                    break;
                case 1:
                    desc.handle_program = 0;
                    break;
                case 2:
                    desc.has_secondary = true;
                    break;
                case 3: {
                    auto flags = fetch->Flags<IR::TextureInstInfo>();
                    flags.descriptor_index.Assign(count);
                    fetch->SetFlags(flags);
                    break;
                }
                case 4:
                    fetch->Invalidate();
                    break;
                case 5:
                    fetch->ReplaceOpcode(IR::Opcode::ImageQueryLod);
                    break;
                case 6:
                    fetch->ReplaceOpcode(IR::Opcode::BoundImageFetch);
                    break;
                case 7: {
                    IR::IREmitter ir{changed.block};
                    static_cast<void>(ir.GetSparseFromOp(IR::Value{fetch}));
                    break;
                }
                }
                CHECK(Vulkan::ComputeUniformTupleReason(changed.program) != Reason::Eligible);
            }
        }
        original.program.info.texture_buffer_descriptors.clear();
        CHECK(Vulkan::ComputeUniformTupleReason(original.program) == Reason::DeclarationCount);
    }
    TupleProgram oversized{input, 4};
    oversized.program.info.texture_buffer_descriptors.push_back(
        oversized.program.info.texture_buffer_descriptors.front());
    CHECK(Vulkan::ComputeUniformTupleReason(oversized.program) == Reason::DeclarationCount);
    oversized.program.stage = Stage::VertexB;
    CHECK(Vulkan::ComputeUniformTupleReason(oversized.program) == Reason::Stage);
}

TEST_CASE("Ordered uniform tuples replay and emit every fetched carrier", "[compute_uniform_tuple]") {
    using Type = UniformTexelType;
    const std::vector<std::vector<Type>> tuples{
        {Type::Uint, Type::Float}, {Type::Float, Type::Uint},
        {Type::Uint, Type::Uint, Type::Uint}, {Type::Sint, Type::Float, Type::Uint, Type::Sint}};
    for (const auto& types : tuples) {
        const auto count = static_cast<u32>(types.size());
        TemporaryRecord record;
        auto captured = [&] {
            TupleInput input;
            input.BeginComputeInputCapture();
            TupleProgram program{input, count};
            const auto info = program.program.info;
            auto snapshot = VideoCommon::ComputeEnvironmentSnapshot::Capture(input);
            std::ofstream output{record.path, std::ios::binary};
            REQUIRE(output.good());
            input.Serialize(output);
            REQUIRE(output.good());
            return std::pair{std::move(snapshot), info};
        }(); // Producer and its transient IR die before either replay is used.
        auto warm = [&] {
            VideoCommon::FileEnvironment environment;
            std::ifstream input{record.path, std::ios::binary};
            environment.Deserialize(input);
            REQUIRE(input.good());
            return VideoCommon::ComputeEnvironmentSnapshot::CaptureDiskCompute(environment);
        }();
        auto cold_environment = captured.first.CreateReplay();
        auto warm_environment = warm.CreateReplay();
        TupleProgram cold{cold_environment, count};
        TupleProgram disk{warm_environment, count};
        REQUIRE(cold.program.info == captured.second);
        REQUIRE(disk.program.info == captured.second);
        REQUIRE(Vulkan::ComputeUniformTupleReason(cold.program) ==
                Vulkan::UniformFamilyReason::Eligible);
        const auto cold_code = cold.Emit(types);
        const auto warm_code = disk.Emit(types);
        CHECK(cold_code == warm_code);
        CheckTuple(cold_code, types);
        CHECK(cold.program.info == disk.program.info);
        auto baseline_environment = captured.first.CreateReplay();
        TupleProgram baseline{baseline_environment, count};
        const std::vector<Type> floats(count, Type::Float);
        CheckTuple(baseline.Emit({}), floats);
        CHECK(cold.program.info == baseline.program.info);
    }
}

TEST_CASE("Uniform tuple keys retain order and exact format support", "[compute_uniform_tuple]") {
    std::array<bool, Vulkan::UniformTupleFormats.size()> supported;
    supported.fill(true);
    const std::array mixed{VK_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R8_UNORM};
    const auto first = Vulkan::MakeUniformTupleKey(mixed, supported);
    REQUIRE(first);
    CHECK(first->types[0] == UniformTexelType::Uint);
    CHECK(first->types[1] == UniformTexelType::Float);
    const std::array swapped{VK_FORMAT_R8_UNORM, VK_FORMAT_R32G32B32A32_UINT};
    const auto second = Vulkan::MakeUniformTupleKey(swapped, supported);
    REQUIRE(second);
    CHECK(*first != *second);
    CHECK(first->Encode() != second->Encode());
    CHECK_FALSE(Vulkan::MakeUniformTupleKey({}, supported));
    const std::array excessive{VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT,
                               VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT};
    CHECK_FALSE(Vulkan::MakeUniformTupleKey(excessive, supported));
    for (size_t i = 0; i < supported.size(); ++i) {
        const std::array format{Vulkan::UniformTupleFormats[i]};
        REQUIRE(Vulkan::MakeUniformTupleKey(format, supported));
        auto unavailable = supported;
        unavailable[i] = false;
        CHECK_FALSE(Vulkan::MakeUniformTupleKey(format, unavailable));
    }
    CHECK_FALSE(Vulkan::MakeUniformTupleKey(std::array{VK_FORMAT_UNDEFINED}, supported));
    CHECK_FALSE(Vulkan::MakeUniformTupleKey(std::array{VK_FORMAT_R8_UINT}, supported));
    std::array<bool, Vulkan::ComputeUniformFormats.size()> old_supported;
    old_supported.fill(true);
    CHECK_FALSE(Vulkan::ComputeUniformType(VK_FORMAT_R8_UNORM, old_supported));
    Vulkan::UniformTupleKey invalid;
    CHECK(invalid.Encode() == ~u32{});
    invalid.count = 5;
    CHECK(invalid.Encode() == ~u32{});
    std::set<u32> encodings;
    for (u32 count = 1, combinations = 3; count <= 4; ++count, combinations *= 3) {
        for (u32 combination = 0; combination < combinations; ++combination) {
            std::array<VkFormat, 4> formats{};
            auto remaining = combination;
            for (u32 i = 0; i < count; ++i) {
                formats[i] = Vulkan::UniformTupleFormats[remaining % 3];
                remaining /= 3;
            }
            const auto key = Vulkan::MakeUniformTupleKey(std::span{formats}.first(count), supported);
            REQUIRE(key);
            CHECK(encodings.insert(key->Encode()).second);
        }
    }
}

TEST_CASE("Final texel tuple observations retain order and reject partial writes",
          "[compute_uniform_tuple]") {
    using Queue = Vulkan::UpdateDescriptorQueue;
    Queue::TexelBufferObservation observer;
    const std::array<size_t, 3> entries{7, 2, 9};
    REQUIRE(observer.Begin(entries));
    const Queue::PreparedTexelBuffer byte_view{VK_NULL_HANDLE, 13, 17, VK_FORMAT_R8_UNORM};
    const Queue::PreparedTexelBuffer half_view{VK_NULL_HANDLE, 42, 10, VK_FORMAT_R16_UINT};
    const Queue::PreparedTexelBuffer rgba_view{VK_NULL_HANDLE, 64, 80, VK_FORMAT_R32G32B32A32_UINT};
    observer.Record(1, byte_view); // Unrelated entry consumes no observation.
    observer.Record(2, half_view);
    observer.Record(9, rgba_view);
    observer.Record(7, byte_view);
    const auto result = observer.Take();
    REQUIRE(result);
    CHECK(result->count == 3);
    CHECK(result->bindings[0].offset == 13);
    CHECK(result->bindings[0].size == 17);
    CHECK(result->bindings[0].format == VK_FORMAT_R8_UNORM);
    CHECK(result->bindings[1].offset == 42);
    CHECK(result->bindings[1].size == 10);
    CHECK(result->bindings[1].format == VK_FORMAT_R16_UINT);
    CHECK(result->bindings[2].format == VK_FORMAT_R32G32B32A32_UINT);
    CHECK_FALSE(observer.Take());
    REQUIRE(observer.Begin(entries));
    observer.Record(7, byte_view);
    observer.Record(2, half_view);
    CHECK_FALSE(observer.Take());
    REQUIRE(observer.Begin(entries));
    observer.Record(7, byte_view);
    observer.Record(2, half_view);
    observer.Record(9, rgba_view);
    observer.Record(9, rgba_view);
    CHECK_FALSE(observer.Take());
    REQUIRE(observer.Begin(entries));
    observer.Reset();
    observer.Record(7, byte_view);
    CHECK_FALSE(observer.Take());
    CHECK_FALSE(observer.Begin(std::array<size_t, 2>{1, 1}));
    CHECK_FALSE(observer.Begin(std::array<size_t, 5>{1, 2, 3, 4, 5}));
    CHECK_FALSE(observer.Begin({}));
}


TEST_CASE("Two-uniform activation keeps exact count and independent format classes",
          "[compute_uniform2]") {
    using Reason = Vulkan::UniformFamilyReason;
    const std::array<const char*, 8> disabled_values{nullptr, "", "0", "true", "01", "1 ", " 1", "2"};
    for (const char* disabled : disabled_values) {
        CHECK_FALSE(Vulkan::ParseMultiUniform2(disabled));
    }
    CHECK(Vulkan::ParseMultiUniform2("1"));
    TupleInput input;
    for (u32 count = 1; count <= 4; ++count) {
        TupleProgram original{input, count};
        CHECK((Vulkan::ComputeUniform2Reason(original.program) == Reason::Eligible) == (count == 2));
        CHECK(Vulkan::IsSingleComputeUniformFetch(original.program) == (count == 1));
    }
    for (u32 entry = 0; entry < 2; ++entry) {
        TupleProgram invalid{input, 2};
        invalid.program.info.texture_buffer_descriptors[entry].has_secondary = true;
        CHECK(Vulkan::ComputeUniform2Reason(invalid.program) == Reason::SecondaryHandle);
    }
    TupleProgram joint{input, 2};
    joint.program.info.image_buffer_descriptors.emplace_back();
    CHECK(Vulkan::ComputeUniform2Reason(joint.program) == Reason::StorageImageDeclarations);
    std::array<bool, Vulkan::UniformTupleFormats.size()> supported;
    supported.fill(true);
    for (size_t first = 0; first < supported.size(); ++first) {
        for (size_t second = 0; second < supported.size(); ++second) {
            const std::array formats{Vulkan::UniformTupleFormats[first],
                                     Vulkan::UniformTupleFormats[second]};
            const auto index = Vulkan::Uniform2FormatIndex(formats, supported);
            REQUIRE(index);
            const Vulkan::Uniform2Types expected{
                Vulkan::UniformTupleTypes[first], Vulkan::UniformTupleTypes[second]};
            CHECK(Vulkan::Uniform2TypesAt(*index) == expected);
            auto unavailable = supported;
            unavailable[second] = false;
            CHECK_FALSE(Vulkan::Uniform2FormatIndex(formats, unavailable));
        }
    }
    CHECK_FALSE(Vulkan::Uniform2FormatIndex(
        {VK_FORMAT_R8_UINT, VK_FORMAT_R32_UINT}, supported));
    CHECK_FALSE(Vulkan::Uniform2FormatIndex(
        {VK_FORMAT_R32_UINT, VK_FORMAT_UNDEFINED}, supported));
    CHECK_FALSE(Vulkan::Uniform2Index(
        {static_cast<UniformTexelType>(3), UniformTexelType::Float}));
    std::set<size_t> indices;
    for (size_t i = 0; i < Vulkan::Uniform2MemberCount; ++i) {
        const auto index = Vulkan::Uniform2Index(Vulkan::Uniform2TypesAt(i));
        REQUIRE(index);
        CHECK(*index == i);
        CHECK(indices.insert(*index).second);
    }
}

TEST_CASE("All nine eager uniform members preserve owned replay and unrelated atomic output",
          "[compute_uniform2]") {
    TemporaryRecord record;
    auto captured = [&] {
        TupleInput input;
        input.BeginComputeInputCapture();
        TupleProgram program{input, 2};
        REQUIRE(Vulkan::ComputeUniform2Reason(program.program) ==
                Vulkan::UniformFamilyReason::Eligible);
        const auto info = program.program.info;
        auto snapshot = VideoCommon::ComputeEnvironmentSnapshot::Capture(input);
        std::ofstream output{record.path, std::ios::binary};
        REQUIRE(output.good());
        input.Serialize(output);
        REQUIRE(output.good());
        return std::pair{std::move(snapshot), info};
    }();
    auto warm = [&] {
        VideoCommon::FileEnvironment input;
        std::ifstream stream{record.path, std::ios::binary};
        input.Deserialize(stream);
        REQUIRE(stream.good());
        return VideoCommon::ComputeEnvironmentSnapshot::CaptureDiskCompute(input);
    }();
    std::optional<Shader::Info> emitted_info;
    for (size_t i = 0; i < Vulkan::Uniform2MemberCount; ++i) {
        const auto types = Vulkan::Uniform2TypesAt(i);
        auto cold_environment = captured.first.CreateReplay();
        auto warm_environment = warm.CreateReplay();
        TupleProgram cold{cold_environment, 2};
        TupleProgram disk{warm_environment, 2};
        REQUIRE(cold.program.info == captured.second);
        REQUIRE(disk.program.info == captured.second);
        REQUIRE(Vulkan::ComputeUniform2Reason(cold.program) ==
                Vulkan::UniformFamilyReason::Eligible);
        const auto code = cold.Emit(types);
        CHECK(code == disk.Emit(types));
        CheckTuple(code, types); // Exact per-component SSBO offset and image atomic chain.
        if (!emitted_info) {
            emitted_info = cold.program.info;
        }
        CHECK(cold.program.info == *emitted_info);
        CHECK(disk.program.info == *emitted_info);
    }
}
