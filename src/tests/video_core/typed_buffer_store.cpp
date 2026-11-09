// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/single_buffer_store.h"
#include "video_core/renderer_vulkan/texel_type_trace_capture.h"
#include "video_core/renderer_vulkan/typed_buffer_store.h"

namespace {
using namespace Shader;

class StoreEnvironment final : public Environment {
public:
    StoreEnvironment() {
        stage = Stage::Compute;
    }
    u64 ReadInstruction(u32) override {
        throw LogicError("Unexpected instruction fetch");
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        REQUIRE(bank == 0);
        REQUIRE(offset == 0x264);
        return 0x99;
    }
    TextureType ReadTextureType(u32) override {
        return TextureType::Buffer;
    }
    TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return TexturePixelFormat::R32_UINT;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return true;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
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
    bool HasHLEMacroState() const override {
        return false;
    }
    bool HasNativeInstructions() const override {
        return true;
    }
    bool IsNativeProgram() const override {
        return true;
    }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

struct StoreProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    StoreEnvironment env;
    IR::Inst* store{};

    StoreProgram() {
        program.stage = Stage::Compute;
        program.workgroup_size = {1, 1, 1};
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        IR::IREmitter ir{block};
        IR::TextureInstInfo flags{};
        flags.type.Assign(TextureType::Buffer);
        flags.image_format.Assign(ImageFormat::Typeless);
        const auto color = ir.CompositeConstruct(ir.Imm32(0xffffffffU), ir.Imm32(0x80000000U),
                                                 ir.Imm32(0x3f800000U), ir.Imm32(0xbf800000U));
        ir.ImageWrite(ir.Imm32(0x264), ir.Imm32(1), color, flags);
        store = &block.back();
        HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        Optimization::TexturePass(env, program, host);
        Optimization::CollectShaderInfoPass(env, program);
    }

    std::vector<u32> Emit(std::optional<ImageBufferStoreType> type) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        RuntimeInfo runtime{};
        runtime.single_image_buffer_store_type = type;
        Backend::Bindings bindings{};
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

std::vector<std::vector<u32>> Instructions(const std::vector<u32>& code, u32 opcode) {
    std::vector<std::vector<u32>> result;
    for (size_t i = 5; i < code.size();) {
        const u32 count = code[i] >> 16;
        REQUIRE(count > 0);
        REQUIRE(i + count <= code.size());
        if ((code[i] & 0xffff) == opcode) {
            result.emplace_back(code.begin() + i + 1, code.begin() + i + count);
        }
        i += count;
    }
    return result;
}
} // namespace

TEST_CASE("Typed buffer family excludes unproven descriptor contracts", "[typed_buffer_store]") {
    for (u32 mutation = 0; mutation < 23; ++mutation) {
        StoreProgram fixture;
        REQUIRE(IsSingleTypelessBufferStore(fixture.program));
        auto& info = fixture.program.info;
        auto& desc = info.image_buffer_descriptors.front();
        auto flags = fixture.store->Flags<IR::TextureInstInfo>();
        IR::IREmitter ir{fixture.block};
        switch (mutation) {
        case 0:
            fixture.program.stage = Stage::Fragment;
            break;
        case 1:
            desc.count = 2;
            break;
        case 2:
            desc.is_read = true;
            break;
        case 3:
            desc.is_written = false;
            break;
        case 4:
            desc.format = ImageFormat::R32_UINT;
            break;
        case 5:
            desc.handle_program = 0;
            break;
        case 6:
            info.handle_programs.push_back({1});
            break;
        case 7:
            info.uses_atomic_image_u32 = true;
            break;
        case 8:
            info.requires_coherent_image_memory = true;
            break;
        case 9:
            info.texture_buffer_descriptors.emplace_back();
            break;
        case 10:
            info.image_descriptors.emplace_back();
            break;
        case 11:
            info.image_buffer_descriptors.push_back(desc);
            break;
        case 12:
            info.texture_descriptors.emplace_back();
            break;
        case 13:
            flags.image_byte_address.Assign(1);
            fixture.store->SetFlags(flags);
            break;
        case 14:
            flags.image_coherent.Assign(1);
            fixture.store->SetFlags(flags);
            break;
        case 15:
            flags.descriptor_index.Assign(1);
            fixture.store->SetFlags(flags);
            break;
        case 16:
            flags.type.Assign(TextureType::Color2D);
            fixture.store->SetFlags(flags);
            break;
        case 17:
            fixture.store->Invalidate();
            break;
        case 18:
            // Eligibility sees normalized post-TexturePass operations.
            ir.ImageRead(IR::Value{}, ir.Imm32(0), flags)
                .Inst()
                ->ReplaceOpcode(IR::Opcode::ImageRead);
            break;
        case 19:
            static_cast<void>(ir.GetAttribute(IR::Attribute::Generic0X));
            break;
        case 20:
            flags.image_format.Assign(ImageFormat::R32_UINT);
            fixture.store->SetFlags(flags);
            break;
        case 21:
            static_cast<void>(
                ir.ImageQueryDimension(IR::Value{}, ir.Imm32(0), ir.Imm1(false), flags));
            break;
        case 22:
            ir.ImageFetch(IR::Value{}, ir.Imm32(0), IR::Value{}, ir.Imm32(0), ir.Imm32(0), flags)
                .Inst()
                ->ReplaceOpcode(IR::Opcode::ImageFetch);
            break;
        }
        CAPTURE(mutation);
        CHECK_FALSE(IsSingleTypelessBufferStore(fixture.program));
        CHECK_THROWS_AS(fixture.Emit(ImageBufferStoreType::Sint), LogicError);
    }
}

TEST_CASE("Typed buffer stores change declaration and four-component bit carrier together",
          "[typed_buffer_store]") {
    for (bool original_integer : {false, true}) {
        std::optional<Info> emitted_info;
        for (auto requested :
             {std::optional<ImageBufferStoreType>{}, std::optional{ImageBufferStoreType::Uint},
              std::optional{ImageBufferStoreType::Sint},
              std::optional{ImageBufferStoreType::Float}}) {
            const auto type = requested.value_or(original_integer ? ImageBufferStoreType::Uint
                                                                  : ImageBufferStoreType::Float);
            StoreProgram fixture; // Emission mutates IR: never reuse an emitted program.
            fixture.program.info.image_buffer_descriptors.front().is_integer = original_integer;
            const auto code = fixture.Emit(requested);
            CHECK(Instructions(code, 111).empty());     // No OpConvertSToF.
            CHECK(Instructions(code, 112).empty());     // No OpConvertUToF.
            const auto images = Instructions(code, 25); // OpTypeImage
            const auto writes = Instructions(code, 99); // OpImageWrite
            REQUIRE(images.size() == 1);
            REQUIRE(writes.size() == 1);
            REQUIRE(images.front().size() >= 8);
            CHECK(images.front()[2] == 5); // Buffer
            CHECK(images.front()[7] == 0); // Unknown format; actual view is unchanged.
            const u32 scalar = images.front()[1];
            const auto scalar_types =
                Instructions(code, type == ImageBufferStoreType::Float ? 22 : 21);
            bool scalar_found{};
            for (const auto& declaration : scalar_types) {
                if (declaration[0] == scalar) {
                    CHECK(declaration[1] == 32);
                    if (type != ImageBufferStoreType::Float) {
                        CHECK(declaration[2] == (type == ImageBufferStoreType::Sint ? 1 : 0));
                    }
                    scalar_found = true;
                }
            }
            REQUIRE(scalar_found);
            const u32 color = writes.front()[2];
            u32 color_type{};
            for (u32 opcode : {80U, 124U}) { // OpCompositeConstruct or OpBitcast
                for (const auto& operation : Instructions(code, opcode)) {
                    if (operation[1] == color) {
                        color_type = operation[0];
                        CHECK(opcode == (type == ImageBufferStoreType::Uint ? 80U : 124U));
                    }
                }
            }
            REQUIRE(color_type != 0);
            bool vector_found{};
            for (const auto& vector : Instructions(code, 23)) { // OpTypeVector
                if (vector[0] == color_type) {
                    CHECK(vector[1] == scalar);
                    CHECK(vector[2] == 4);
                    vector_found = true;
                }
            }
            CHECK(vector_found);
            if (emitted_info) {
                CHECK(fixture.program.info == *emitted_info);
            }
            emitted_info = fixture.program.info;
        }
    }
}

TEST_CASE("Typed buffer selection requires the exact promoted host format features",
          "[typed_buffer_store]") {
    using namespace Vulkan;
    const std::array<bool, 4> enabled{true, true, true, true};
    CHECK(TypedBufferStoreType(VK_FORMAT_R32_UINT, enabled) == ImageBufferStoreType::Uint);
    CHECK(TypedBufferStoreType(VK_FORMAT_R32_SINT, enabled) == ImageBufferStoreType::Sint);
    CHECK(TypedBufferStoreType(VK_FORMAT_R32_SFLOAT, enabled) == ImageBufferStoreType::Float);
    CHECK(TypedBufferStoreType(VK_FORMAT_R32G32B32A32_SFLOAT, enabled) ==
          ImageBufferStoreType::Float);
    CHECK_FALSE(TypedBufferStoreType(VK_FORMAT_R32G32_UINT, enabled));
    CHECK_FALSE(TypedBufferStoreType(VK_FORMAT_R8_UNORM, enabled));
    CHECK_FALSE(TypedBufferStoreType(VK_FORMAT_UNDEFINED, enabled));
    for (size_t i = 0; i < enabled.size(); ++i) {
        auto partial = enabled;
        partial[i] = false;
        CHECK_FALSE(TypedBufferStoreType(TypedBufferStoreFormats[i], partial));
    }
    CHECK_FALSE(SupportsTypedBufferStore(0));
    CHECK_FALSE(SupportsTypedBufferStore(VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT));
    CHECK_FALSE(SupportsTypedBufferStore(VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT));
    CHECK(SupportsTypedBufferStore(VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                                   VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT));
}

TEST_CASE("Selected typed class reserves the final diagnostic quota before sealing",
          "[typed_buffer_store]") {
    namespace Trace = Vulkan::TexelTypeTrace;
    using VideoCore::Surface::PixelFormat;
    StoreProgram fixture;
    Trace::Budget budget;
    int queue{};
    Trace::Scope scope{&queue, true, true, budget};
    Trace::Capture(scope, fixture.program.info, {}, {},
                   fixture.program.info.image_buffer_descriptors.front(), 0, 1, 1,
                   PixelFormat::R32_SINT, PixelFormat::R32_SINT, 0, 4, 0, Trace::Numeric::Sint);
    Trace::Scope::Host(&queue, 0, 5, 0, 4, VK_FORMAT_R32_SINT);
    auto sealed = Trace::Scope::SealCurrent();
    REQUIRE(sealed);
    REQUIRE(sealed->size == 1);
    CHECK(sealed->rows[0].compiled == Trace::Numeric::Sint);
    CHECK_FALSE(sealed->rows[0].mismatch);
    CHECK(sealed->rows[0].host_observed);
    CHECK(sealed->rows[0].limit == Trace::Budget::ControlLimit);
}
