// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "video_core/graphics_input_capture.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "video_core/renderer_vulkan/typed_uniform_buffer.h"

namespace {
// Only the query producer is synthetic. The recorder, immutable replay, missing-query
// rejection and compiler-cache isolation exercised below are production classes.
class GraphicsQueries : public Shader::Environment {
public:
    GraphicsQueries() {
        stage = Shader::Stage::VertexB;
        start_address = 0x100;
        is_proprietary_driver = true;
        std::memset(&sph, 0x12, sizeof(sph));
        gp_passthrough_mask = {1, 2, 3, 4, 5, 6, 7, 8};
    }

    u32 generation{};
    size_t queries{};
    bool native{true};

    void SetStage(Shader::Stage value) {
        stage = value;
    }
    u64 ReadInstruction(u32 address) override {
        ++queries;
        return (u64{generation} << 32) | address;
    }
    u32 TranslateProgramAddress(u32 address) const override {
        return address + 16 * generation;
    }
    bool IsNativeProgram() const override {
        return native;
    }
    bool HasNativeInstructions() const override {
        return true;
    }
    bool HasHLEMacroState() const override {
        return true;
    }
    u32 TextureBoundBuffer() const override {
        return 3;
    }
    u32 LocalMemorySize() const override {
        return 64;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        ++queries;
        return bank + offset + generation;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return generation == 0 ? Shader::TextureType::Buffer : Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return generation == 0 ? Shader::TexturePixelFormat::R32_UINT
                               : Shader::TexturePixelFormat::R32_SINT;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return generation == 0;
    }
    bool IsCbufStorageBufferPointer(u32, u32) override {
        return generation == 0;
    }
    bool IsCbufIndirectStorageBufferPointer(u32, u32, u32) override {
        return generation == 0;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return generation == 0 ? std::optional{Shader::ReplaceConstant::BaseVertex} : std::nullopt;
    }
    u32 ReadViewportTransformState() override {
        return generation;
    }
    std::vector<std::pair<u32, u32>> NullStoragePointerSlots() const override {
        return {{3, generation}};
    }
    std::vector<u64> MappedStoragePointerKeys() const override {
        return {u64{3} << 32 | generation};
    }
    void Dump(u64, u64) override {}
};

void QueryAll(Shader::Environment& environment) {
    CHECK(environment.ReadInstruction(0x150) == 0x150);
    CHECK(environment.TranslateProgramAddress(0x150) == 0x150);
    CHECK(environment.ReadCbufValue(3, 8) == 11);
    CHECK(environment.ReadTextureType(11) == Shader::TextureType::Buffer);
    CHECK(environment.ReadTexturePixelFormat(11) == Shader::TexturePixelFormat::R32_UINT);
    CHECK(environment.IsTexturePixelFormatInteger(11));
    CHECK(environment.IsCbufStorageBufferPointer(3, 0x20));
    CHECK(environment.IsCbufIndirectStorageBufferPointer(3, 0x20, 8));
    CHECK(environment.GetReplaceConstBuffer(3, 0x30) == Shader::ReplaceConstant::BaseVertex);
    CHECK(environment.ReadViewportTransformState() == 0);
    const std::vector<std::pair<u32, u32>> nulls{{3, 0}};
    const std::vector<u64> mapped{u64{3} << 32};
    CHECK(environment.NullStoragePointerSlots() == nulls);
    CHECK(environment.MappedStoragePointerKeys() == mapped);
}
} // namespace

TEST_CASE("Actual VertexB translation replays only recorded compiler queries",
          "[graphics_input_capture]") {
    class ExitVertex final : public GraphicsQueries {
    public:
        ExitVertex() {
            sph = {};
            start_address = 0;
            gp_passthrough_mask = {};
        }
        u64 ReadInstruction(u32 address) override {
            ++queries;
            if (address != sizeof(Shader::ProgramHeader)) {
                throw Shader::LogicError("Unexpected synthetic VertexB instruction address");
            }
            // Same verified Maxwell EXIT encoding used by compute_input_replay.cpp.
            return 0xe30000000007000fULL;
        }
    } source;
    const auto translate = [](Shader::Environment& environment) {
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow;
        Shader::ObjectPool<Shader::IR::Inst> instructions;
        Shader::ObjectPool<Shader::IR::Block> blocks;
        Shader::Maxwell::Flow::CFG cfg{
            environment, flow,
            static_cast<u32>(environment.StartAddress() + sizeof(Shader::ProgramHeader))};
        auto program = Shader::Maxwell::TranslateProgram(instructions, blocks, environment, cfg,
                                                        Shader::HostTranslateInfo{});
        REQUIRE(program.stage == Shader::Stage::VertexB);
        Shader::Profile profile{};
        profile.supported_spirv = 0x00010300;
        Shader::Backend::Bindings bindings{};
        return Shader::Backend::SPIRV::EmitSPIRV(profile, Shader::RuntimeInfo{}, program, bindings);
    };
    VideoCommon::GraphicsInputCapture capture{source};
    const auto original = translate(capture);
    const size_t producer_queries = source.queries;
    REQUIRE(producer_queries > 0);
    auto first = capture.CreateReplay();
    auto second = capture.CreateReplay();
    source.generation = 1;
    CHECK(translate(first) == original);
    CHECK(translate(second) == original);
    CHECK(source.queries == producer_queries);
}

TEST_CASE("Graphics replay owns its recorded answers after producer destruction",
          "[graphics_input_capture]") {
    std::unique_ptr<VideoCommon::GraphicsInputCapture::Replay> replay;
    {
        GraphicsQueries source;
        VideoCommon::GraphicsInputCapture capture{source};
        QueryAll(capture);
        capture.cbuf_word_cache.emplace(Shader::CbufWordKey{3, 8}, 999);
        replay = std::make_unique<VideoCommon::GraphicsInputCapture::Replay>(capture.CreateReplay());
        source.generation = 1;
        QueryAll(*replay);
        CHECK(source.queries == 2);
        CHECK(replay->cbuf_word_cache.empty());
        CHECK(replay->handle_cache.empty());
        CHECK(replay->track_cache.empty());
        CHECK(std::memcmp(&replay->SPH(), &source.SPH(), sizeof(Shader::ProgramHeader)) == 0);
        CHECK(replay->GpPassthroughMask() == source.GpPassthroughMask());
    }
    QueryAll(*replay);
    CHECK(replay->ShaderStage() == Shader::Stage::VertexB);
    CHECK(replay->StartAddress() == 0x100);
    CHECK(replay->IsProprietaryDriver());
    CHECK(replay->IsNativeProgram());
    CHECK(replay->HasNativeInstructions());
    CHECK(replay->HasHLEMacroState());
    CHECK(replay->TextureBoundBuffer() == 3);
    CHECK(replay->LocalMemorySize() == 64);
    CHECK(replay->SharedMemorySize() == 0);
    const std::array<u32, 3> group{1, 1, 1};
    CHECK(replay->WorkgroupSize() == group);
}

TEST_CASE("Graphics replay rejects uncaptured queries without live fallback",
          "[graphics_input_capture]") {
    GraphicsQueries source;
    VideoCommon::GraphicsInputCapture capture{source};
    static_cast<void>(capture.ReadInstruction(0x150));
    auto replay = capture.CreateReplay();
    CHECK_THROWS_AS(replay.ReadInstruction(0x158), Shader::LogicError);
    CHECK_THROWS_AS(replay.TranslateProgramAddress(0x150), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadCbufValue(3, 8), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTextureType(11), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadTexturePixelFormat(11), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsTexturePixelFormatInteger(11), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufStorageBufferPointer(3, 8), Shader::LogicError);
    CHECK_THROWS_AS(replay.IsCbufIndirectStorageBufferPointer(3, 8, 0), Shader::LogicError);
    CHECK_THROWS_AS(replay.GetReplaceConstBuffer(3, 8), Shader::LogicError);
    CHECK_THROWS_AS(replay.ReadViewportTransformState(), Shader::LogicError);
    CHECK_THROWS_AS(replay.NullStoragePointerSlots(), Shader::LogicError);
    CHECK_THROWS_AS(replay.MappedStoragePointerKeys(), Shader::LogicError);
    CHECK(source.queries == 1);
}

TEST_CASE("Graphics capture forwards changing answers but refuses inconsistent replay",
          "[graphics_input_capture]") {
    GraphicsQueries source;
    VideoCommon::GraphicsInputCapture capture{source};
    QueryAll(capture);
    auto stable = capture.CreateReplay();
    source.generation = 1;
    SECTION("Constant buffer answer") {
        CHECK(capture.ReadCbufValue(3, 8) == 12);
    }
    SECTION("Instruction answer") {
        CHECK(capture.ReadInstruction(0x150) == ((u64{1} << 32) | 0x150));
    }
    SECTION("Native address translation") {
        CHECK(capture.TranslateProgramAddress(0x150) == 0x160);
    }
    SECTION("Optional replacement answer") {
        CHECK_FALSE(capture.GetReplaceConstBuffer(3, 0x30));
    }
    SECTION("Scalar optional-record overload") {
        CHECK(capture.ReadViewportTransformState() == 1);
    }
    SECTION("Vector optional-record overload") {
        const std::vector<std::pair<u32, u32>> expected{{3, 1}};
        CHECK(capture.NullStoragePointerSlots() == expected);
    }
    CHECK_THROWS_AS(capture.CreateReplay(), Shader::LogicError);
    QueryAll(stable);
}

TEST_CASE("Graphics capture requires native VertexB and a bounded query count",
          "[graphics_input_capture]") {
    GraphicsQueries source;
    SECTION("VertexA is not an emitted VertexB substitute") {
        source.SetStage(Shader::Stage::VertexA);
        CHECK_THROWS_AS(VideoCommon::GraphicsInputCapture{source}, Shader::LogicError);
    }
    SECTION("Non-native input is not eligible") {
        source.native = false;
        CHECK_THROWS_AS(VideoCommon::GraphicsInputCapture{source}, Shader::LogicError);
    }
    SECTION("Empty instruction capture is not replayable") {
        VideoCommon::GraphicsInputCapture capture{source};
        CHECK_THROWS_AS(capture.CreateReplay(), Shader::LogicError);
    }
    SECTION("Overflow preserves returned input but disables replay") {
        VideoCommon::GraphicsInputCapture capture{source};
        for (u32 address = 0; address <= 32768; ++address) {
            CHECK(capture.ReadInstruction(address) == address);
        }
        CHECK_THROWS_AS(capture.CreateReplay(), Shader::LogicError);
        CHECK(source.queries == 32769);
    }
}

TEST_CASE("Vertex uniform policy rejects other stage and resource contracts",
          "[graphics_input_capture][typed_uniform_buffer]") {
    Shader::ObjectPool<Shader::IR::Inst> pool;
    Shader::IR::Block block{pool};
    Shader::IR::Program program;
    program.stage = Shader::Stage::VertexB;
    program.blocks = {&block};
    Shader::TextureBufferDescriptor descriptor{};
    descriptor.count = 1;
    program.info.texture_buffer_descriptors.push_back(descriptor);
    Shader::IR::IREmitter ir{block};
    Shader::IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::Buffer);
    flags.descriptor_index.Assign(0);
    const auto fetch = ir.ImageFetch(ir.Imm32(0), ir.Imm32(2), {}, ir.Imm32(0),
                                    Shader::IR::U32{}, flags);
    fetch.InstRecursive()->ReplaceOpcode(Shader::IR::Opcode::ImageFetch);
    REQUIRE(Vulkan::IsSingleVertexUniformFetch(program));
    SECTION("VertexA is excluded") {
        program.stage = Shader::Stage::VertexA;
    }
    SECTION("Task metadata is excluded") {
        program.info.is_task = true;
    }
    SECTION("Generated layer geometry is excluded") {
        program.info.requires_layer_emulation = true;
    }
    SECTION("Arrays are excluded") {
        program.info.texture_buffer_descriptors[0].count = 2;
    }
    SECTION("Computed handles are excluded") {
        program.info.texture_buffer_descriptors[0].handle_program = 0;
    }
    SECTION("Additional uniform declaration is excluded") {
        program.info.texture_buffer_descriptors.push_back(descriptor);
    }
    SECTION("Mixed image buffers are excluded") {
        program.info.image_buffer_descriptors.emplace_back();
    }
    SECTION("Sparse residency is excluded") {
        static_cast<void>(ir.GetSparseFromOp(fetch));
    }
    SECTION("A different descriptor index is excluded") {
        flags.descriptor_index.Assign(1);
        fetch.InstRecursive()->SetFlags(flags);
    }
    SECTION("A non-buffer fetch is excluded") {
        flags.type.Assign(Shader::TextureType::Color2D);
        fetch.InstRecursive()->SetFlags(flags);
    }
    CHECK_FALSE(Vulkan::IsSingleVertexUniformFetch(program));
}

TEST_CASE("Vertex uniform format selection uses exact supported host numeric class",
          "[graphics_input_capture][typed_uniform_buffer]") {
    const std::array<bool, 3> all{true, true, true};
    CHECK(Vulkan::TypedUniformType(VK_FORMAT_R32_SFLOAT, all) == Shader::UniformTexelType::Float);
    CHECK(Vulkan::TypedUniformType(VK_FORMAT_R32_UINT, all) == Shader::UniformTexelType::Uint);
    CHECK(Vulkan::TypedUniformType(VK_FORMAT_R32_SINT, all) == Shader::UniformTexelType::Sint);
    CHECK_FALSE(Vulkan::TypedUniformType(VK_FORMAT_UNDEFINED, all));
    CHECK_FALSE(Vulkan::TypedUniformType(VK_FORMAT_R32G32B32A32_UINT, all));
    const std::array<bool, 3> no_signed{true, true, false};
    CHECK_FALSE(Vulkan::TypedUniformType(VK_FORMAT_R32_SINT, no_signed));
    CHECK(Vulkan::TypedUniformType(VK_FORMAT_R32_UINT, no_signed) == Shader::UniformTexelType::Uint);
}
