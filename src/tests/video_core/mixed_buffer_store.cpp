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
#include "shader_recompiler/image_buffer_access.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/single_buffer_store.h"
#include "video_core/shader_environment.h"

namespace {
using namespace Shader;

// Synthetic producer; actual TexturePass, metadata collection, replay and emitter.
class MixedStoreInput final : public VideoCommon::GenericEnvironment {
public:
    MixedStoreInput() {
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
        const auto type = handle == 0x11 ? TextureType::Buffer : TextureType::Color2D;
        RecordComputeTextureType(handle, type);
        return type;
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

struct MixedStoreProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    IR::Inst* store{};
    IR::Inst* atomic{};
    Backend::Bindings emitted_bindings{};

    explicit MixedStoreProgram(Environment& environment, bool target_alias = false) {
        program.stage = Stage::Compute;
        program.workgroup_size = environment.WorkgroupSize();
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
        ir.ImageWrite(ir.Imm32(0x100), ir.Imm32(1), color, flags);
        store = &block.back();
        if (target_alias) {
            atomic = ir.ImageAtomicIAdd(ir.Imm32(0x100), ir.Imm32(2), ir.Imm32(7), flags).Inst();
        } else {
            flags.type.Assign(TextureType::Color2D);
            flags.image_format.Assign(ImageFormat::R32_UINT);
            atomic = ir.ImageAtomicIAdd(ir.Imm32(0x104),
                                       ir.CompositeConstruct(ir.Imm32(2), ir.Imm32(3)),
                                       ir.Imm32(7), flags).Inst();
        }
        HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        Optimization::TexturePass(environment, program, host);
        Optimization::CollectShaderInfoPass(environment, program);
    }

    std::vector<u32> Emit(ImageBufferStoreType type) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        RuntimeInfo runtime{};
        runtime.single_image_buffer_store_type = type;
        runtime.enable_shader_loop_safety = true;
        runtime.enable_bloom_workaround = false;
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, emitted_bindings);
    }
};

std::vector<std::vector<u32>> Ops(const std::vector<u32>& code, u32 opcode) {
    std::vector<std::vector<u32>> result;
    for (size_t at = 5; at < code.size();) {
        const u32 count = code[at] >> 16;
        REQUIRE(count > 0);
        REQUIRE(at + count <= code.size());
        if ((code[at] & 0xffff) == opcode) {
            result.emplace_back(code.begin() + at + 1, code.begin() + at + count);
        }
        at += count;
    }
    return result;
}

void CheckUntouchedAtomic(const std::vector<u32>& code, u32 opcode = 234) {
    const auto atomics = Ops(code, opcode); // OpAtomicIAdd, SMin or And.
    REQUIRE(atomics.size() == 1);
    REQUIRE(atomics.front().size() == 6);
    const auto integers = Ops(code, 21);
    CHECK(std::ranges::any_of(integers, [&](const auto& type) {
        return type[0] == atomics.front()[0] && type[1] == 32 && type[2] == 0;
    }));
    const auto pointers = Ops(code, 60); // OpImageTexelPointer
    REQUIRE(pointers.size() == 1);
    CHECK(atomics.front()[2] == pointers.front()[1]);
    const auto variables = Ops(code, 59);
    const auto variable = std::ranges::find_if(variables, [&](const auto& item) {
        return item[1] == pointers.front()[2];
    });
    REQUIRE(variable != variables.end());
    const auto pointer_types = Ops(code, 32);
    const auto pointer_type = std::ranges::find_if(pointer_types, [&](const auto& item) {
        return item[0] == (*variable)[0];
    });
    REQUIRE(pointer_type != pointer_types.end());
    const auto images = Ops(code, 25);
    CHECK(std::ranges::any_of(images, [&](const auto& image) {
        return image[0] == (*pointer_type)[2] && image[1] == atomics.front()[0] &&
               image[2] == 1 && image[6] == 2 &&
               image[7] == 33; // 2D, storage, R32ui; not the specialized buffer.
    }));
    const auto constants = Ops(code, 43);
    for (const auto [id, expected] : std::array<std::pair<u32, u32>, 4>{
             {{atomics.front()[3], 1}, {atomics.front()[4], 0},
              {atomics.front()[5], 7}, {pointers.front()[4], 0}}}) {
        CHECK(std::ranges::any_of(constants, [&](const auto& item) {
            return item[1] == id && item[2] == expected;
        }));
    }
}

void CheckStore(const std::vector<u32>& code, ImageBufferStoreType type) {
    const auto images = Ops(code, 25);
    REQUIRE(images.size() == 2);
    const auto buffer = std::ranges::find_if(images, [](const auto& image) {
        return image[2] == 5 && image[6] == 2 && image[7] == 0;
    });
    REQUIRE(buffer != images.end());
    const u32 scalar = (*buffer)[1];
    const auto scalars = Ops(code, type == ImageBufferStoreType::Float ? 22 : 21);
    CHECK(std::ranges::any_of(scalars, [&](const auto& item) {
        return item[0] == scalar && item[1] == 32 &&
               (type == ImageBufferStoreType::Float ||
                item[2] == (type == ImageBufferStoreType::Sint ? 1U : 0U));
    }));
    const auto writes = Ops(code, 99);
    REQUIRE(writes.size() == 1);
    const auto color_ops = Ops(code, type == ImageBufferStoreType::Uint ? 80 : 124);
    const auto color = std::ranges::find_if(color_ops, [&](const auto& item) {
        return item[1] == writes.front()[2];
    });
    REQUIRE(color != color_ops.end());
    const auto vectors = Ops(code, 23);
    CHECK(std::ranges::any_of(vectors, [&](const auto& item) {
        return item[0] == (*color)[0] && item[1] == scalar && item[2] == 4;
    }));
    const u32 raw_color = type == ImageBufferStoreType::Uint ? (*color)[1] : (*color)[2];
    const auto composites = Ops(code, 80);
    const auto raw = std::ranges::find_if(composites, [&](const auto& item) {
        return item[1] == raw_color;
    });
    REQUIRE(raw != composites.end());
    REQUIRE(raw->size() == 6);
    const auto constants = Ops(code, 43);
    constexpr std::array expected{0xffffffffU, 0x80000000U, 0x3f800000U, 0xbf800000U};
    for (size_t component = 0; component < expected.size(); ++component) {
        CHECK(std::ranges::any_of(constants, [&](const auto& item) {
            return item[1] == (*raw)[component + 2] && item[2] == expected[component];
        }));
    }
    CHECK(Ops(code, 111).empty()); // No numeric SInt/UInt-to-float conversions.
    CHECK(Ops(code, 112).empty());
}

struct TemporaryRecord {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("orbitals-mixed-store-replay-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    ~TemporaryRecord() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
} // namespace

TEST_CASE("Storage classification distinguishes descriptor namespaces and merged atomics",
          "[mixed_buffer_store]") {
    MixedStoreInput input;
    MixedStoreProgram fixture{input};
    REQUIRE(fixture.program.info.uses_atomic_image_u32);
    REQUIRE(fixture.program.info.image_buffer_descriptors.size() == 1);
    REQUIRE(fixture.program.info.image_descriptors.size() == 1);
    const auto access = ClassifyImageBufferAccess(fixture.program, 0);
    CHECK(access.complete);
    CHECK(access.target_write);
    CHECK_FALSE(access.target_read);
    CHECK_FALSE(access.target_atomic);
    REQUIRE(access.other_atomic_counts.size() == 1);
    CHECK(access.other_atomic_counts[0] == 1);
    CHECK(IsSingleTypelessBufferStore(fixture.program));

    MixedStoreProgram alias{input, true};
    REQUIRE(alias.program.info.image_buffer_descriptors.size() == 1);
    REQUIRE(alias.program.info.image_descriptors.empty());
    CHECK(alias.program.info.image_buffer_descriptors.front().is_read);
    const auto merged = ClassifyImageBufferAccess(alias.program, 0);
    CHECK(merged.complete);
    CHECK(merged.target_read);
    CHECK(merged.target_write);
    CHECK(merged.target_atomic);
    CHECK_FALSE(IsSingleTypelessBufferStore(alias.program));

    // Signed/bitwise semantics must survive while the unrelated buffer changes type.
    for (const auto [opcode, spirv_opcode] :
         std::array<std::pair<IR::Opcode, u32>, 2>{
             {{IR::Opcode::ImageAtomicSMin32, 236}, {IR::Opcode::ImageAtomicAnd32, 240}}}) {
        MixedStoreProgram variant{input};
        variant.atomic->ReplaceOpcode(opcode);
        REQUIRE(IsSingleTypelessBufferStore(variant.program));
        const auto code = variant.Emit(ImageBufferStoreType::Float);
        CheckUntouchedAtomic(code, spirv_opcode);
        CheckStore(code, ImageBufferStoreType::Float);
        CHECK(Ops(code, 234).empty());
    }
}

TEST_CASE("Mixed store eligibility fails closed on unsupported target or image contracts",
          "[mixed_buffer_store]") {
    MixedStoreInput input;
    for (u32 mutation = 0; mutation < 27; ++mutation) {
        MixedStoreProgram fixture{input};
        REQUIRE(IsSingleTypelessBufferStore(fixture.program));
        auto& info = fixture.program.info;
        auto& target = info.image_buffer_descriptors.front();
        auto& image = info.image_descriptors.front();
        auto flags = fixture.atomic->Flags<IR::TextureInstInfo>();
        switch (mutation) {
        case 0:
            target.is_read = true;
            break;
        case 1:
            target.count = 2;
            break;
        case 2:
            target.handle_program = 0;
            break;
        case 3:
            info.texture_buffer_descriptors.emplace_back();
            break;
        case 4:
            info.texture_descriptors.emplace_back();
            break;
        case 5:
            info.image_buffer_descriptors.push_back(target);
            break;
        case 6:
            info.uses_atomic_image_u32 = false;
            break;
        case 7:
            info.requires_coherent_image_memory = true;
            break;
        case 8:
            flags.descriptor_index.Assign(1);
            fixture.atomic->SetFlags(flags);
            break;
        case 9:
            flags.type.Assign(TextureType::Buffer);
            fixture.atomic->SetFlags(flags);
            break;
        case 10:
            image.count = 2;
            break;
        case 11:
            image.handle_program = 0;
            break;
        case 12:
            image.format = ImageFormat::Typeless;
            break;
        case 13:
            image.is_read = false;
            break;
        case 14:
            fixture.atomic->ReplaceOpcode(IR::Opcode::BoundImageAtomicIAdd32);
            break;
        case 15:
            fixture.atomic->Invalidate();
            break;
        case 16:
            fixture.store->Invalidate();
            break;
        case 17: {
            auto store_flags = fixture.store->Flags<IR::TextureInstInfo>();
            store_flags.image_coherent.Assign(1);
            fixture.store->SetFlags(store_flags);
            break;
        }
        case 18: {
            IR::IREmitter ir{fixture.block};
            auto query = ir.ImageQueryDimension(IR::Value{}, ir.Imm32(0), ir.Imm1(false), flags);
            query.Inst()->ReplaceOpcode(IR::Opcode::ImageQueryDimensions);
            break;
        }
        case 19:
            flags.image_byte_address.Assign(1);
            fixture.atomic->SetFlags(flags);
            break;
        case 20:
            info.image_descriptors.push_back(image);
            break;
        case 21:
            image.is_integer = false;
            break;
        case 22:
            fixture.atomic->ReplaceOpcode(IR::Opcode::ImageAtomicInc32);
            break;
        case 23:
            fixture.atomic->ReplaceOpcode(IR::Opcode::ImageAtomicDec32);
            break;
        case 24:
            fixture.atomic->SetArg(0, IR::Value{u32{1}});
            break;
        case 25: {
            IR::IREmitter ir{fixture.block};
            const auto read = ir.ImageRead(ir.Imm32(0),
                ir.CompositeConstruct(ir.Imm32(0), ir.Imm32(0)), flags);
            read.Inst()->ReplaceOpcode(IR::Opcode::ImageRead);
            CHECK(ClassifyImageBufferAccess(fixture.program, 0).other_read_or_write);
            break;
        }
        case 26: {
            IR::IREmitter ir{fixture.block};
            const auto color = ir.CompositeConstruct(ir.Imm32(1), ir.Imm32(2),
                                                     ir.Imm32(3), ir.Imm32(4));
            ir.ImageWrite(ir.Imm32(0), ir.CompositeConstruct(ir.Imm32(0), ir.Imm32(0)),
                          color, flags);
            fixture.block.back().ReplaceOpcode(IR::Opcode::ImageWrite);
            CHECK(ClassifyImageBufferAccess(fixture.program, 0).other_read_or_write);
            break;
        }
        }
        CAPTURE(mutation);
        CHECK_FALSE(IsSingleTypelessBufferStore(fixture.program));
    }
}

TEST_CASE("Mixed store cold and warm replays retain atomics and all descriptor metadata",
          "[mixed_buffer_store]") {
    MixedStoreInput source;
    source.BeginComputeInputCapture();
    MixedStoreProgram original{source};
    const auto expected = original.program.info;
    const auto cold = VideoCommon::ComputeEnvironmentSnapshot::Capture(source);
    TemporaryRecord record;
    {
        std::ofstream stream{record.path, std::ios::binary};
        REQUIRE(stream.good());
        source.Serialize(stream);
        REQUIRE(stream.good());
    }
    VideoCommon::FileEnvironment disk;
    {
        std::ifstream stream{record.path, std::ios::binary};
        disk.Deserialize(stream);
        REQUIRE(stream.good());
    }
    const auto warm = VideoCommon::ComputeEnvironmentSnapshot::CaptureDiskCompute(disk);
    std::optional<Info> emitted_info;
    std::optional<std::array<u32, 7>> emitted_bindings;
    for (auto type : {ImageBufferStoreType::Uint, ImageBufferStoreType::Sint,
                      ImageBufferStoreType::Float}) {
        auto cold_env = cold.CreateReplay();
        auto warm_env = warm.CreateReplay();
        MixedStoreProgram first{cold_env};
        MixedStoreProgram second{warm_env};
        REQUIRE(first.program.info == expected);
        REQUIRE(second.program.info == expected);
        REQUIRE(IsSingleTypelessBufferStore(first.program));
        REQUIRE(IsSingleTypelessBufferStore(second.program));
        const auto first_code = first.Emit(type);
        const auto second_code = second.Emit(type);
        CHECK(first_code == second_code);
        CheckUntouchedAtomic(first_code);
        CheckStore(first_code, type);
        CHECK(first.program.info == second.program.info);
        const auto signature = [](const Backend::Bindings& value) {
            return std::array{value.unified, value.uniform_buffer, value.storage_buffer,
                              value.texture, value.image, value.texture_scaling_index,
                              value.image_scaling_index};
        };
        const auto bindings = signature(first.emitted_bindings);
        CHECK(bindings == signature(second.emitted_bindings));
        if (emitted_bindings) {
            CHECK(bindings == *emitted_bindings);
        }
        emitted_bindings = bindings;
        if (emitted_info) {
            CHECK(first.program.info == *emitted_info);
        }
        emitted_info = first.program.info;
    }
}
