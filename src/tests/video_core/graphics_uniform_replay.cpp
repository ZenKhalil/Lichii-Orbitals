// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/graphics_input_capture.h"
#include "video_core/renderer_vulkan/typed_uniform_buffer.h"

namespace {
using namespace Shader;

// Synthetic encodings derived from maxwell.inc and the production MOV32I,
// texture_load.cpp, load_constant.h and load_store_memory.cpp field definitions.
// No captured program or game instruction bytes are used.
constexpr std::array<u64, 5> FetchAndStore{
    0x010000000007f000ULL | 8 | (u64{2} << 20), // MOV32I R8, 2
    0xdc00000000070000ULL | (u64{7} << 51) | (u64{15} << 31) |
        (u64{8} << 8) | (u64{255} << 20) | (u64{2} << 36),
    // TLD.1D.LZ R0.xyzw, R8, bound c3[8], no sparse result.
    0xef90000000070000ULL | 10 | (u64{255} << 8) | (u64{0x20} << 20) |
        (u64{3} << 36) | (u64{5} << 48), // LDC.U64 R10, c3[0x20]
    0xeed8000000070000ULL | (u64{10} << 8) | (u64{1} << 45) |
        (u64{6} << 48), // STG.E.128 [R10], R0..R3
    0xe30000000007000fULL, // EXIT
};

constexpr u32 InstructionAddress(u32 index) {
    return sizeof(ProgramHeader) + index * 8 + ((index + 1) / 3) * 8;
}

class FetchVertexInput final : public Environment {
public:
    FetchVertexInput() {
        stage = Stage::VertexB;
        start_address = 0;
        is_proprietary_driver = true;
    }

    size_t queries{};

    u64 ReadInstruction(u32 address) override {
        ++queries;
        for (u32 index = 0; index < FetchAndStore.size(); ++index) {
            if (address == InstructionAddress(index)) {
                return FetchAndStore[index];
            }
        }
        throw LogicError("Synthetic VertexB instruction outside its recorded program");
    }
    bool IsNativeProgram() const override {
        return true;
    }
    bool HasNativeInstructions() const override {
        return true;
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    u32 TextureBoundBuffer() const override {
        return 3;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        ++queries;
        if (bank == 3 && offset == 8) {
            return 11;
        }
        if (bank == 3 && offset == 0x20) {
            return 0x1000;
        }
        if (bank == 3 && offset == 0x24) {
            return 0;
        }
        throw LogicError("Unexpected synthetic VertexB constant-buffer query");
    }
    TextureType ReadTextureType(u32 handle) override {
        ++queries;
        if (handle != 11) {
            throw LogicError("Unexpected synthetic VertexB texture handle");
        }
        return TextureType::Buffer;
    }
    TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        static_cast<void>(ReadTextureType(handle));
        return TexturePixelFormat::R32_UINT;
    }
    bool IsTexturePixelFormatInteger(u32 handle) override {
        static_cast<void>(ReadTextureType(handle));
        return true;
    }
    bool IsCbufStorageBufferPointer(u32 bank, u32 offset) override {
        ++queries;
        return bank == 3 && offset == 0x20;
    }
    bool IsCbufIndirectStorageBufferPointer(u32, u32, u32) override {
        throw LogicError("Synthetic VertexB has no indirect pointer table");
    }
    std::vector<u64> MappedStoragePointerKeys() const override {
        return {(u64{3} << 32) | 0x20};
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {
        // Synthetic compiler input has no disk dump.
    }
};

struct VertexTranslation {
    ObjectPool<Maxwell::Flow::Block> flow;
    ObjectPool<IR::Inst> instructions;
    ObjectPool<IR::Block> blocks;
    IR::Program program;

    explicit VertexTranslation(Environment& environment) {
        HostTranslateInfo host{};
        host.min_ssbo_alignment = 16;
        host.ApplyDescriptorLimitPolicy();
        const Maxwell::TranslationOptions options{.enable_rescaling = false, .verify_ir = true};
        Maxwell::Flow::CFG cfg{environment, flow,
                              static_cast<u32>(environment.StartAddress() + sizeof(ProgramHeader))};
        program = Maxwell::TranslateProgram(instructions, blocks, environment, cfg, host, &options);
    }

    std::vector<u32> Emit(std::optional<UniformTexelType> type, Backend::Bindings& bindings) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        profile.unified_descriptor_binding = true;
        RuntimeInfo runtime{};
        runtime.enable_shader_loop_safety = true;
        if (type) {
            runtime.uniform_texel_types = {*type};
        }
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

size_t Count(const IR::Program& program, IR::Opcode opcode) {
    size_t count{};
    for (const auto* block : program.blocks) {
        for (const auto& instruction : block->Instructions()) {
            count += instruction.GetOpcode() == opcode;
        }
    }
    return count;
}

using Operations = std::vector<std::vector<u32>>;
Operations Ops(std::span<const u32> code, u32 opcode) {
    Operations result;
    for (size_t at = 5; at < code.size();) {
        const size_t count = code[at] >> 16;
        REQUIRE(count > 0);
        REQUIRE(at + count <= code.size());
        if ((code[at] & 0xffff) == opcode) {
            result.emplace_back(code.begin() + at + 1, code.begin() + at + count);
        }
        at += count;
    }
    return result;
}

auto BindingTuple(const Backend::Bindings& value) {
    return std::tuple{value.unified, value.uniform_buffer, value.storage_buffer, value.texture,
                      value.image, value.texture_scaling_index, value.image_scaling_index};
}

// Track only bit-preserving carrier operations. Numeric conversion cannot make a
// fetched component reach a checked store through this graph. This is an emitted
// dataflow check, not a substitute for a Vulkan fetched-value execution test.
void CheckCarrierStores(std::span<const u32> code, UniformTexelType type) {
    const auto fetches = Ops(code, 95); // OpImageFetch
    REQUIRE(fetches.size() == 1);
    REQUIRE(fetches[0].size() >= 4);
    const auto& fetch = fetches[0];
    const auto vectors = Ops(code, 23);
    const auto vector = std::ranges::find_if(vectors, [&](const auto& value) {
        return value[0] == fetch[0];
    });
    REQUIRE(vector != vectors.end());
    CHECK((*vector)[2] == 4);
    const u32 scalar = (*vector)[1];
    const auto scalars = Ops(code, type == UniformTexelType::Float ? 22 : 21);
    CHECK(std::ranges::any_of(scalars, [&](const auto& value) {
        return value[0] == scalar && value[1] == 32 &&
               (type == UniformTexelType::Float ||
                value[2] == (type == UniformTexelType::Sint ? 1U : 0U));
    }));

    std::map<u32, std::vector<u32>> origins;
    origins.emplace(fetch[1], std::vector<u32>{0, 1, 2, 3});
    std::map<u32, u32> pointer_storage;
    for (const auto& pointer : Ops(code, 32)) { // OpTypePointer
        REQUIRE(pointer.size() == 3);
        pointer_storage.emplace(pointer[0], pointer[1]);
    }
    std::map<u32, u32> pointer_types;
    std::array<bool, 4> stored{};
    for (size_t at = 5; at < code.size();) {
        const size_t count = code[at] >> 16;
        const u32 opcode = code[at] & 0xffff;
        const auto args = code.subspan(at + 1, count - 1);
        if (opcode == 59 || opcode == 65 || opcode == 66 || opcode == 67 || opcode == 70) {
            // Variables and ordinary/in-bounds access chains retain the pointer result type.
            pointer_types[args[1]] = args[0];
        }
        if (opcode == 124 || opcode == 83) { // OpBitcast or OpCopyObject
            const auto source = origins.find(args[2]);
            if (source != origins.end()) {
                origins[args[1]] = source->second;
            }
        } else if (opcode == 81 && args.size() == 4) { // OpCompositeExtract, one index
            const auto source = origins.find(args[2]);
            if (source != origins.end() && args[3] < source->second.size()) {
                origins[args[1]] = {source->second[args[3]]};
            }
        } else if (opcode == 80) { // OpCompositeConstruct
            std::vector<u32> parts;
            bool complete = true;
            for (size_t index = 2; index < args.size(); ++index) {
                const auto part = origins.find(args[index]);
                if (part == origins.end()) {
                    complete = false;
                    break;
                }
                parts.insert(parts.end(), part->second.begin(), part->second.end());
            }
            if (complete) {
                origins[args[1]] = std::move(parts);
            }
        } else if (opcode == 62) { // OpStore
            const auto pointer = pointer_types.find(args[0]);
            if (pointer == pointer_types.end()) {
                at += count;
                continue;
            }
            const auto storage = pointer_storage.find(pointer->second);
            if (storage == pointer_storage.end() || storage->second != 12) {
                // Only StorageBuffer writes count; a local/private spill is insufficient.
                at += count;
                continue;
            }
            const auto source = origins.find(args[1]);
            if (source != origins.end()) {
                for (u32 component : source->second) {
                    REQUIRE(component < stored.size());
                    stored[component] = true;
                }
            }
        }
        at += count;
    }
    CHECK(std::ranges::all_of(stored, [](bool value) { return value; }));
}
} // namespace

TEST_CASE("Synthetic VertexB buffer fetch survives actual translation into raw carrier stores",
          "[graphics_uniform_replay]") {
    const std::array expected{Maxwell::Opcode::MOV32I, Maxwell::Opcode::TLD, Maxwell::Opcode::LDC,
                              Maxwell::Opcode::STG, Maxwell::Opcode::EXIT};
    for (size_t index = 0; index < expected.size(); ++index) {
        REQUIRE(Maxwell::Decode(FetchAndStore[index]) == expected[index]);
    }
    auto source = std::make_unique<FetchVertexInput>();
    VideoCommon::GraphicsInputCapture capture{*source};
    VertexTranslation original{capture};
    REQUIRE(Vulkan::IsSingleVertexUniformFetch(original.program));
    REQUIRE(Count(original.program, IR::Opcode::ImageFetch) == 1);
    REQUIRE(Count(original.program, IR::Opcode::WriteStorage128) == 1);
    REQUIRE(original.program.info.storage_buffers_descriptors.size() == 1);
    CHECK(original.program.info.storage_buffers_descriptors[0].cbuf_index == 3);
    CHECK(original.program.info.storage_buffers_descriptors[0].cbuf_offset == 0x20);
    const auto original_info = original.program.info;
    auto default_replay = capture.CreateReplay();
    std::array replays{capture.CreateReplay(), capture.CreateReplay(), capture.CreateReplay()};
    source.reset();

    VertexTranslation unspecialized{default_replay};
    Backend::Bindings default_bindings{};
    const auto default_code = unspecialized.Emit(std::nullopt, default_bindings);
    for (size_t index = 0; index < replays.size(); ++index) {
        const auto type = static_cast<UniformTexelType>(index);
        VertexTranslation member{replays[index]};
        CHECK(member.program.info == original_info);
        CHECK(Count(member.program, IR::Opcode::ImageFetch) == 1);
        CHECK(Count(member.program, IR::Opcode::WriteStorage128) == 1);
        Backend::Bindings bindings{};
        const auto code = member.Emit(type, bindings);
        CHECK(BindingTuple(bindings) == BindingTuple(default_bindings));
        CHECK(member.program.info == unspecialized.program.info);
        CheckCarrierStores(code, type);
        if (type == UniformTexelType::Float) {
            CHECK(code == default_code);
        }
    }
}
