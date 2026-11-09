// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/shader_environment.h"

namespace {
namespace IR = Shader::IR;
using VideoCommon::ComputeEnvironmentSnapshot;

constexpr u64 Key(u32 bank, u32 offset) {
    return (u64{bank} << 32) | offset;
}

constexpr u32 Address(u32 instruction) {
    return 0x50 + instruction * 8 + ((instruction + 1) / 3) * 8;
}

// Synthetic input producer: capture, replay getters, CFG, translator and SPIR-V emitter below
// are production code. This fixture does not initialize guest memory or a Vulkan device.
class CapturedInput final : public VideoCommon::GenericEnvironment {
public:
    CapturedInput() {
        stage = Shader::Stage::Compute;
        start_address = 0x50;
        cached_lowest = 0x50;
        cached_highest = 0x60;
        code = {0x123456789abcdef0, 0xfedcba9876543210, 0};
        initial_offset = 0x50;
        local_memory_size = 64;
        shared_memory_size = 4096;
        workgroup_size = {32, 2, 1};
        texture_bound = 7;
        viewport_transform_state = 3;
        is_proprietary_driver = true;
        std::memset(&sph, 0x12, sizeof(sph));
        gp_passthrough_mask = {1, 2, 3, 4, 5, 6, 7, 8};
        mesh_registers = {9, 10, 11};
        std::memset(&mesh_primitive_header, 0x34, sizeof(mesh_primitive_header));
    }

    u32 word{0x12345678};
    Shader::TextureType texture_type{Shader::TextureType::Buffer};
    Shader::TexturePixelFormat format{Shader::TexturePixelFormat::R32_UINT};
    size_t producer_queries{};

    u32 ReadCbufValue(u32 bank, u32 offset) override {
        ++producer_queries;
        RecordComputeCbufValue(Key(bank, offset), word);
        return word;
    }
    Shader::TextureType ReadTextureType(u32 handle) override {
        ++producer_queries;
        RecordComputeTextureType(handle, texture_type);
        return texture_type;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        ++producer_queries;
        RecordComputeTextureFormat(handle, format);
        return format;
    }
    bool IsTexturePixelFormatInteger(u32 handle) override {
        return ReadTexturePixelFormat(handle) == Shader::TexturePixelFormat::R32_UINT;
    }
    u32 ReadViewportTransformState() override {
        return viewport_transform_state;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 bank, u32 offset) override {
        const auto found = cbuf_replacements.find(Key(bank, offset));
        return found == cbuf_replacements.end() ? std::nullopt : std::optional{found->second};
    }
    void SetPointerAnswers() {
        storage_pointer_types.emplace(Key(1, 0x20), 1);
        storage_pointer_types.emplace(Key(1, 0x28), 0);
        storage_pointer_types.emplace((u64{1} << 63) | (u64{0x38} << 40) | Key(1, 0x20), 1);
    }
    void SetReplacement() {
        has_hle_engine_state = true;
        cbuf_replacements.emplace(Key(2, 0x10), Shader::ReplaceConstant::BaseVertex);
    }
    void SetNative() {
        native_source_size = 16;
    }
    void AddNativeMap() {
        auto lowered = std::make_shared<VideoCommon::LoweredNativeCode>();
        lowered->source_size = 32;
        lowered->code.resize(16, 0);
        Shader::Ampere::AppendNativeAddressMap(lowered->code, std::array<u32, 2>{0, 1});
        // The actual live initializer establishes bounds after the address map is appended.
        // An empty trap prefix needs no memory manager; this is not the full engine constructor.
        InitializeNativeCode(std::move(lowered));
    }
    void MarkUncached() {
        has_unbound_instructions = true;
    }
    void BreakBounds() {
        cached_highest += 8;
    }
    void SetOtherStage() {
        stage = Shader::Stage::Fragment;
    }
    void ReplaceCode(std::span<const u64> instructions) {
        cached_lowest = 0x50;
        cached_highest = Address(static_cast<u32>(instructions.size()));
        code.assign((cached_highest - cached_lowest) / 8 + 1, 0);
        for (u32 i = 0; i < instructions.size(); ++i) {
            code[(Address(i) - cached_lowest) / 8] = instructions[i];
        }
    }
};

u64 Branch(u32 from, u32 to, IR::FlowTest test) {
    const u32 offset = Address(to) - Address(from) - 8;
    return 0xe240000000070000ULL | (u64{offset & 0xffffff} << 20) | static_cast<u64>(test);
}

std::array<u64, 11> FlagPhiInstructions() {
    return {
        0xf0c8000000070000ULL | (u64{33} << 20), // S2R R0, SR_TID.X
        0x1c00000000070000ULL | 1 | (u64{0xffffffff} << 20) | (u64{1} << 52),
        0x1c00000000070000ULL | 2 | (u64{255} << 8) | (u64{1} << 53), // IADD.X
        0x010000000007f000ULL | 3 | (u64{7} << 20),
        Branch(4, 7, IR::FlowTest::EQ),
        0x010000000007f000ULL | 3 | (u64{9} << 20),
        Branch(6, 8, IR::FlowTest::T),
        0x010000000007f000ULL | 3 | (u64{11} << 20),
        0xef58000000070000ULL | 2 | (u64{255} << 8) | (u64{4} << 48),
        0xef58000000070000ULL | 3 | (u64{255} << 8) | (u64{4} << 48) | (u64{4} << 20),
        0xe30000000007000fULL,
    };
}

struct Translation {
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow;
    Shader::ObjectPool<IR::Inst> instructions;
    Shader::ObjectPool<IR::Block> blocks;
    IR::Program program;

    explicit Translation(Shader::Environment& env) {
        Shader::Maxwell::Flow::CFG cfg{env, flow, env.StartAddress()};
        program = Shader::Maxwell::TranslateProgram(instructions, blocks, env, cfg,
                                                    Shader::HostTranslateInfo{});
    }
};

// DumpProgram includes host instruction addresses, which are not compiler inputs.
std::string CanonicalIR(const IR::Program& program) {
    const auto dump = IR::DumpProgram(program);
    std::string result;
    size_t begin{};
    while (begin < dump.size()) {
        const size_t end = dump.find('\n', begin);
        const size_t length = (end == std::string::npos ? dump.size() : end) - begin;
        const auto line = std::string_view{dump}.substr(begin, length);
        const bool host_address =
            line.size() >= 19 && line[0] == '[' && line[17] == ']' && line[18] == ' ';
        result.append(line.substr(host_address ? 19 : 0));
        result.push_back('\n');
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

size_t Count(const IR::Program& program, IR::Opcode opcode) {
    size_t result{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            result += inst.GetOpcode() == opcode;
        }
    }
    return result;
}
} // namespace

TEST_CASE("Compute capture owns exact metadata and query answers after source destruction",
          "[shader][compute_input_replay]") {
    auto source = std::make_unique<CapturedInput>();
    source->BeginComputeInputCapture();
    CHECK(source->ReadCbufValue(3, 0x18) == 0x12345678);
    CHECK(source->ReadTextureType(9) == Shader::TextureType::Buffer);
    CHECK(source->ReadTexturePixelFormat(9) == Shader::TexturePixelFormat::R32_UINT);
    source->SetPointerAnswers();
    source->SetReplacement();
    source->SetNative();
    auto snapshot = ComputeEnvironmentSnapshot::Capture(*source);
    CHECK(source->producer_queries == 3);
    auto first = snapshot.CreateReplay();
    CHECK(first.ShaderStage() == source->ShaderStage());
    CHECK(first.StartAddress() == source->StartAddress());
    CHECK(first.IsProprietaryDriver() == source->IsProprietaryDriver());
    CHECK(std::memcmp(&first.SPH(), &source->SPH(), sizeof(Shader::ProgramHeader)) == 0);
    CHECK(first.GpPassthroughMask() == source->GpPassthroughMask());
    CHECK(first.MeshRegisters() == source->MeshRegisters());
    CHECK(std::memcmp(&first.MeshPrimitiveHeader(), &source->MeshPrimitiveHeader(),
                      sizeof(Shader::ProgramHeader)) == 0);
    source.reset();
    CHECK(first.IsNativeProgram());
    CHECK(first.HasNativeInstructions());
    CHECK(first.HasHLEMacroState());
    CHECK(first.TextureBoundBuffer() == 7);
    CHECK(first.LocalMemorySize() == 64);
    CHECK(first.SharedMemorySize() == 4096);
    CHECK(first.WorkgroupSize() == std::array<u32, 3>{32, 2, 1});
    CHECK(first.ReadViewportTransformState() == 3);
    CHECK(first.ReadInstruction(0x50) == 0x123456789abcdef0);
    CHECK(first.ReadInstruction(0x58) == 0xfedcba9876543210);
    CHECK_THROWS_AS(first.ReadInstruction(0x60), Shader::LogicError);
    CHECK(first.ReadCbufValue(3, 0x18) == 0x12345678);
    CHECK(first.ReadTextureType(9) == Shader::TextureType::Buffer);
    CHECK(first.IsTexturePixelFormatInteger(9));
    CHECK(first.IsCbufStorageBufferPointer(1, 0x20));
    CHECK_FALSE(first.IsCbufStorageBufferPointer(1, 0x28));
    CHECK(first.IsCbufIndirectStorageBufferPointer(1, 0x20, 0x38));
    CHECK(first.GetReplaceConstBuffer(2, 0x10) == Shader::ReplaceConstant::BaseVertex);
    CHECK_FALSE(first.GetReplaceConstBuffer(2, 0x14));
    first.cbuf_word_cache.emplace(Shader::CbufWordKey{0, 4}, 99);
    first.handle_cache.emplace(Shader::HandleKey{}, 99);
    auto second = snapshot.CreateReplay();
    CHECK(second.cbuf_word_cache.empty());
    CHECK(second.handle_cache.empty());
    CHECK(second.track_cache.empty());
    CHECK(second.ReadCbufValue(3, 0x18) == 0x12345678);
}

TEST_CASE("Compute replay refuses missing inputs and invalid capture states",
          "[shader][compute_input_replay]") {
    CapturedInput source;
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(source), Shader::LogicError);
    source.BeginComputeInputCapture();
    CHECK_THROWS_AS(source.BeginComputeInputCapture(), Shader::LogicError);
    auto snapshot = ComputeEnvironmentSnapshot::Capture(source);
    auto replay = snapshot.CreateReplay();
    CHECK_FALSE(replay.IsNativeProgram());
    CHECK(replay.IsProprietaryDriver()); // Preserve true even though texture_bound != 2.
    for (u32 address : {0x48U, 0x51U, 0x68U, 0xffffffffU}) {
        CHECK_THROWS_AS(replay.ReadInstruction(address), Shader::LogicError);
    }
    CHECK_THROWS_AS(replay.ReadCbufValue(3, 0), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTextureType(9), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTexturePixelFormat(9), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufStorageBufferPointer(0, 0), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufIndirectStorageBufferPointer(0, 0, 8), Shader::LogicError);
    CHECK(replay.TranslateProgramAddress(0x1234) == 0x1234);
    source.MarkUncached();
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(source), Shader::LogicError);
    CapturedInput invalid_bounds;
    invalid_bounds.BeginComputeInputCapture();
    invalid_bounds.BreakBounds();
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(invalid_bounds), Shader::LogicError);
    CapturedInput wrong_stage;
    wrong_stage.SetOtherStage();
    CHECK_THROWS_AS(wrong_stage.BeginComputeInputCapture(), Shader::LogicError);
    CapturedInput too_late;
    static_cast<void>(too_late.ReadCbufValue(0, 0));
    CHECK_THROWS_AS(too_late.BeginComputeInputCapture(), Shader::LogicError);
}

TEST_CASE("Capture records conflicts without changing producer return values",
          "[shader][compute_input_replay]") {
    for (u32 kind = 0; kind < 3; ++kind) {
        CapturedInput source;
        source.BeginComputeInputCapture();
        if (kind == 0) {
            CHECK(source.ReadCbufValue(0, 0) == 0x12345678);
            source.word = 55;
            CHECK(source.ReadCbufValue(0, 0) == 55);
        } else if (kind == 1) {
            CHECK(source.ReadTextureType(1) == Shader::TextureType::Buffer);
            source.texture_type = Shader::TextureType::Color2D;
            CHECK(source.ReadTextureType(1) == Shader::TextureType::Color2D);
        } else {
            CHECK(source.ReadTexturePixelFormat(1) == Shader::TexturePixelFormat::R32_UINT);
            source.format = Shader::TexturePixelFormat::R32_FLOAT;
            CHECK(source.ReadTexturePixelFormat(1) == Shader::TexturePixelFormat::R32_FLOAT);
        }
        CHECK(source.producer_queries == 2);
        CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(source), Shader::LogicError);
    }
    CapturedInput stable;
    stable.BeginComputeInputCapture();
    for (u32 i = 0; i < 3; ++i) {
        static_cast<void>(stable.ReadCbufValue(0, 0));
        static_cast<void>(stable.ReadTextureType(1));
        static_cast<void>(stable.ReadTexturePixelFormat(1));
    }
    CHECK_NOTHROW(ComputeEnvironmentSnapshot::Capture(stable));
}

TEST_CASE("Fresh captured translations restore Phi and consumed flag operations",
          "[shader][compute_input_replay][spirv]") {
    CapturedInput source;
    const auto code = FlagPhiInstructions();
    source.ReplaceCode(code);
    source.BeginComputeInputCapture();
    Translation original{source};
    const auto snapshot = ComputeEnvironmentSnapshot::Capture(source);
    const auto original_ir = CanonicalIR(original.program);
    INFO(original_ir);
    REQUIRE(Count(original.program, IR::Opcode::Phi) > 0);
    REQUIRE(Count(original.program, IR::Opcode::GetCarryFromOp) > 0);
    REQUIRE(Count(original.program, IR::Opcode::GetZeroFromOp) > 0);
    REQUIRE(Count(original.program, IR::Opcode::WriteSharedU32) == 2);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    const auto original_spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, original.program);
    CHECK(Count(original.program, IR::Opcode::GetCarryFromOp) == 0);
    for (u32 iteration = 0; iteration < 3; ++iteration) {
        auto replay = snapshot.CreateReplay();
        Translation fresh{replay};
        CHECK(CanonicalIR(fresh.program) == original_ir);
        CHECK(fresh.program.shared_memory_size == 4096);
        CHECK(fresh.program.workgroup_size == std::array<u32, 3>{32, 2, 1});
        REQUIRE(Count(fresh.program, IR::Opcode::Phi) > 0);
        REQUIRE(Count(fresh.program, IR::Opcode::GetCarryFromOp) > 0);
        const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, fresh.program);
        CHECK(spirv == original_spirv);
        CHECK(Count(fresh.program, IR::Opcode::GetCarryFromOp) == 0);
    }
}

TEST_CASE("Captured native address maps and forced pointer answers survive replay",
          "[shader][compute_input_replay]") {
    CapturedInput source;
    source.AddNativeMap();
    REQUIRE(source.LoweredNative());
    CHECK(source.CachedSizeWords() == source.LoweredNative()->code.size());
    CHECK(source.Analyze().has_value());
    const size_t cached_bytes = source.CachedSizeBytes();
    CHECK_NOTHROW(source.SetCachedSize(cached_bytes));
    CHECK(source.CachedSizeBytes() == cached_bytes);
    CHECK_THROWS_AS(source.SetCachedSize(cached_bytes + 8), Shader::RuntimeError);
    source.SetPointerAnswers();
    const std::array forced{Key(1, 0x28)};
    source.KeepStoragePointers(forced);
    source.BeginComputeInputCapture();
    auto snapshot = ComputeEnvironmentSnapshot::Capture(source);
    auto replay = snapshot.CreateReplay();
    const u32 exclusive_high = static_cast<u32>(cached_bytes - sizeof(u64));
    CHECK(replay.ReadInstruction(exclusive_high - sizeof(u64)) ==
          source.ReadInstruction(exclusive_high - sizeof(u64)));
    CHECK_THROWS_AS(source.ReadInstruction(exclusive_high), Shader::RuntimeError);
    CHECK_THROWS_AS(replay.ReadInstruction(exclusive_high), Shader::LogicError);
    CHECK(replay.TranslateProgramAddress(0) == 0x50);
    CHECK(replay.TranslateProgramAddress(0) == source.TranslateProgramAddress(0));
    CHECK(replay.TranslateProgramAddress(16) == 0x58);
    CHECK(replay.TranslateProgramAddress(16) == source.TranslateProgramAddress(16));
    CHECK_THROWS_AS(replay.TranslateProgramAddress(8), Shader::RuntimeError);
    CHECK_THROWS_AS(replay.TranslateProgramAddress(32), Shader::RuntimeError);
    CHECK(replay.IsNativeProgram());
    CHECK(replay.HasNativeInstructions());
    CHECK(replay.IsCbufStorageBufferPointer(1, 0x28));
    CHECK(replay.MappedStoragePointerKeys() == source.MappedStoragePointerKeys());
    CHECK(replay.NullStoragePointerSlots() == source.NullStoragePointerSlots());
    CapturedInput late_force;
    late_force.SetPointerAnswers();
    late_force.BeginComputeInputCapture();
    CHECK_FALSE(late_force.IsCbufStorageBufferPointer(1, 0x28));
    late_force.KeepStoragePointers(forced);
    CHECK(late_force.IsCbufStorageBufferPointer(1, 0x28));
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(late_force), Shader::LogicError);
    CapturedInput late_code;
    late_code.AddNativeMap();
    late_code.BeginComputeInputCapture();
    CHECK(late_code.Analyze().has_value()); // Native Analyze only hashes; it does not replace code.
    CHECK_NOTHROW(ComputeEnvironmentSnapshot::Capture(late_code));
    late_code.AddNativeMap(); // Actual protected initializer invalidates even equal replacement.
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(late_code), Shader::LogicError);
}

TEST_CASE("Unarmed query recording preserves existing changed-answer behavior",
          "[shader][compute_input_replay]") {
    CapturedInput source;
    CHECK(source.ReadCbufValue(0, 0) == 0x12345678);
    source.word = 13;
    CHECK(source.ReadCbufValue(0, 0) == 13);
    CHECK(source.ReadTextureType(0) == Shader::TextureType::Buffer);
    source.texture_type = Shader::TextureType::Color2D;
    CHECK(source.ReadTextureType(0) == Shader::TextureType::Color2D);
    CHECK(source.ReadTexturePixelFormat(0) == Shader::TexturePixelFormat::R32_UINT);
    source.format = Shader::TexturePixelFormat::R32_FLOAT;
    CHECK(source.ReadTexturePixelFormat(0) == Shader::TexturePixelFormat::R32_FLOAT);
    CHECK(source.producer_queries == 6);
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(source), Shader::LogicError);
}

namespace {
// Contains only synthetic test bytes. The snapshot API itself performs no I/O.
struct ReplayRecordFile {
    std::filesystem::path path;
    ReplayRecordFile() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("eden-compute-replay-" + std::to_string(nonce));
        REQUIRE(std::filesystem::create_directory(path));
    }
    ~ReplayRecordFile() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    VideoCommon::FileEnvironment Load(const CapturedInput& source,
                                      bool inconsistent_bounds = false) const {
        const auto file = path / "synthetic.env";
        {
            std::ofstream output{file, std::ios::binary};
            source.Serialize(output);
            REQUIRE(output.good());
        }
        if (inconsistent_bounds) {
            // The existing serialized header has six u64 fields, then four u32 fields
            // before cached_highest. Change only that bound; retain all payload bytes.
            std::fstream edit{file, std::ios::binary | std::ios::in | std::ios::out};
            edit.seekp(6 * sizeof(u64) + 4 * sizeof(u32));
            const u32 invalid_high = 0x68;
            edit.write(reinterpret_cast<const char*>(&invalid_high), sizeof(invalid_high));
            REQUIRE(edit.good());
        }
        VideoCommon::FileEnvironment result;
        {
            std::ifstream input{file, std::ios::binary};
            result.Deserialize(input);
            REQUIRE(input.good());
        }
        return result;
    }
};
} // namespace

TEST_CASE("Warm compute snapshot preserves loaded record answers and inclusive bounds",
          "[shader][compute_input_replay][pipeline_cache_files]") {
    ReplayRecordFile file;
    CapturedInput source;
    source.SetPointerAnswers();
    static_cast<void>(source.ReadCbufValue(3, 0x18));
    static_cast<void>(source.ReadTextureType(9));
    static_cast<void>(source.ReadTexturePixelFormat(9));
    auto loaded = file.Load(source);
    CHECK_FALSE(loaded.IsProprietaryDriver()); // Existing disk inference: texture_bound != 2.
    CHECK(loaded.ReadInstruction(0x60) == 0);  // Existing disk endpoint is inclusive.
    auto snapshot = ComputeEnvironmentSnapshot::CaptureDiskCompute(loaded);
    // Destroy the loaded record and remove its backing file before any replay queries.
    loaded = VideoCommon::FileEnvironment{};
    REQUIRE(std::filesystem::remove(file.path / "synthetic.env"));
    auto replay = snapshot.CreateReplay();
    CHECK_FALSE(replay.IsProprietaryDriver());
    CHECK_FALSE(replay.IsNativeProgram());
    CHECK(replay.ReadInstruction(0x60) == 0);
    CHECK(replay.ReadCbufValue(3, 0x18) == 0x12345678);
    CHECK(replay.ReadTextureType(9) == Shader::TextureType::Buffer);
    CHECK(replay.ReadTexturePixelFormat(9) == Shader::TexturePixelFormat::R32_UINT);
    CHECK(replay.IsCbufStorageBufferPointer(1, 0x20));
    CHECK_FALSE(replay.IsCbufStorageBufferPointer(1, 0x28));
    CHECK(replay.IsCbufIndirectStorageBufferPointer(1, 0x20, 0x38));
    CHECK_THROWS_AS(replay.ReadInstruction(0x68), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadCbufValue(3, 0x1c), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTextureType(8), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTexturePixelFormat(8), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufStorageBufferPointer(1, 0x30), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufIndirectStorageBufferPointer(1, 0x20, 0x40), Shader::LogicError);
    replay.cbuf_word_cache.emplace(Shader::CbufWordKey{0, 0}, 7);
    replay.handle_cache.emplace(Shader::HandleKey{}, 8);
    auto fresh = snapshot.CreateReplay();
    CHECK(fresh.cbuf_word_cache.empty());
    CHECK(fresh.handle_cache.empty());
    CHECK(fresh.track_cache.empty());
}

TEST_CASE("Warm compute snapshot retains native maps and rejects noncompute or malformed records",
          "[shader][compute_input_replay][pipeline_cache_files]") {
    ReplayRecordFile file;
    CapturedInput native;
    native.AddNativeMap();
    auto loaded = file.Load(native);
    REQUIRE(loaded.IsNativeProgram());
    const auto snapshot = ComputeEnvironmentSnapshot::CaptureDiskCompute(loaded);
    auto replay = snapshot.CreateReplay();
    CHECK(replay.TranslateProgramAddress(0) == 0x50);
    CHECK(replay.TranslateProgramAddress(16) == 0x58);
    CHECK(replay.HasNativeInstructions());
    CHECK(replay.IsProprietaryDriver() == loaded.IsProprietaryDriver());
    CHECK(replay.ReadInstruction(static_cast<u32>(native.CachedSizeBytes() - 8)) ==
          Shader::Ampere::NativeAddressMapMagic);
    CHECK_THROWS_AS(replay.TranslateProgramAddress(32), Shader::RuntimeError);
    VideoCommon::FileEnvironment empty;
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::CaptureDiskCompute(empty), Shader::LogicError);
    CapturedInput malformed;
    auto invalid = file.Load(malformed, true);
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::CaptureDiskCompute(invalid), Shader::LogicError);
    CapturedInput wrong_stage;
    wrong_stage.SetOtherStage();
    auto graphics = file.Load(wrong_stage);
    CHECK_THROWS_AS(ComputeEnvironmentSnapshot::CaptureDiskCompute(graphics), Shader::LogicError);
}

TEST_CASE("Warm captured compiler inputs reproduce fresh flag and Phi translations",
          "[shader][compute_input_replay][pipeline_cache_files][spirv]") {
    ReplayRecordFile file;
    CapturedInput source;
    source.ReplaceCode(FlagPhiInstructions());
    auto loaded = file.Load(source);
    Translation original{loaded};
    const auto original_ir = CanonicalIR(original.program);
    INFO(original_ir);
    REQUIRE(Count(original.program, IR::Opcode::Phi) > 0);
    REQUIRE(Count(original.program, IR::Opcode::GetCarryFromOp) > 0);
    auto snapshot = ComputeEnvironmentSnapshot::CaptureDiskCompute(loaded);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    const auto expected = Shader::Backend::SPIRV::EmitSPIRV(profile, original.program);
    for (u32 iteration = 0; iteration < 3; ++iteration) {
        auto replay = snapshot.CreateReplay();
        Translation fresh{replay};
        CHECK(CanonicalIR(fresh.program) == original_ir);
        REQUIRE(Count(fresh.program, IR::Opcode::Phi) > 0);
        REQUIRE(Count(fresh.program, IR::Opcode::GetCarryFromOp) > 0);
        CHECK(Shader::Backend::SPIRV::EmitSPIRV(profile, fresh.program) == expected);
    }
}

TEST_CASE("Disk pipeline serialization rejects conflicting captured inputs without altering cache",
          "[shader][compute_input_replay][pipeline_cache_files]") {
    for (u32 kind = 0; kind < 4; ++kind) {
        CAPTURE(kind);
        ReplayRecordFile temporary;
        const auto path = temporary.path / "synthetic-pipeline-cache.bin";
        constexpr u64 key = 0x12345678;
        constexpr u32 version = 0x10203;
        CapturedInput good;
        good.SetPointerAnswers();
        REQUIRE(good.CanBeSerialized());
        VideoCommon::SerializePipeline(
            key, std::array<const VideoCommon::GenericEnvironment*, 1>{&good}, path, version);
        const auto read_bytes = [&] {
            std::ifstream input{path, std::ios::binary};
            REQUIRE(input.good());
            return std::string{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        };
        const auto before = read_bytes();
        REQUIRE_FALSE(before.empty());

        CapturedInput inconsistent;
        inconsistent.BeginComputeInputCapture();
        if (kind == 0) {
            CHECK(inconsistent.ReadCbufValue(0, 0) == 0x12345678);
            inconsistent.word = 55;
            CHECK(inconsistent.ReadCbufValue(0, 0) == 55);
        } else if (kind == 1) {
            CHECK(inconsistent.ReadTextureType(1) == Shader::TextureType::Buffer);
            inconsistent.texture_type = Shader::TextureType::Color2D;
            CHECK(inconsistent.ReadTextureType(1) == Shader::TextureType::Color2D);
        } else if (kind == 2) {
            CHECK(inconsistent.ReadTexturePixelFormat(1) == Shader::TexturePixelFormat::R32_UINT);
            inconsistent.format = Shader::TexturePixelFormat::R32_FLOAT;
            CHECK(inconsistent.ReadTexturePixelFormat(1) == Shader::TexturePixelFormat::R32_FLOAT);
        } else {
            inconsistent.SetPointerAnswers();
            CHECK_FALSE(inconsistent.IsCbufStorageBufferPointer(1, 0x28));
            inconsistent.KeepStoragePointers(std::array{Key(1, 0x28)});
            CHECK(inconsistent.IsCbufStorageBufferPointer(1, 0x28));
        }
        CHECK_THROWS_AS(ComputeEnvironmentSnapshot::Capture(inconsistent), Shader::LogicError);
        CHECK_FALSE(inconsistent.CanBeSerialized());
        VideoCommon::SerializePipeline(
            key, std::array<const VideoCommon::GenericEnvironment*, 1>{&inconsistent}, path, version);
        CHECK(read_bytes() == before);

        size_t records{};
        VideoCommon::LoadPipelines(
            std::stop_token{}, path, version,
            [&](std::ifstream& input, VideoCommon::FileEnvironment environment) {
                u64 loaded_key{};
                input.read(reinterpret_cast<char*>(&loaded_key), sizeof(loaded_key));
                REQUIRE(input.good());
                CHECK(loaded_key == key);
                if (records++ == 0) {
                    CHECK(environment.IsCbufStorageBufferPointer(1, 0x20));
                    CHECK_FALSE(environment.IsCbufStorageBufferPointer(1, 0x28));
                }
            },
            [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {
                FAIL("Unexpected graphics record in synthetic compute cache");
            });
        CHECK(records == 1);
    }
    CapturedInput stable;
    stable.BeginComputeInputCapture();
    for (u32 repeat = 0; repeat < 3; ++repeat) {
        static_cast<void>(stable.ReadCbufValue(0, 0));
        static_cast<void>(stable.ReadTextureType(1));
        static_cast<void>(stable.ReadTexturePixelFormat(1));
    }
    CHECK(stable.CanBeSerialized());
    stable.MarkUncached();
    CHECK_FALSE(stable.CanBeSerialized());
}
