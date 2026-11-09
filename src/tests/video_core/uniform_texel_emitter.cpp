// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

namespace {
using namespace Shader;

struct FetchProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;

    explicit FetchProgram(u32 count, bool sparse = false, bool query = false) {
        program.stage = Stage::VertexB;
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        IR::IREmitter ir{block};
        for (u32 index = 0; index < count; ++index) {
            TextureBufferDescriptor descriptor{};
            descriptor.count = 1;
            descriptor.cbuf_offset = index * 4;
            program.info.texture_buffer_descriptors.push_back(descriptor);
            IR::TextureInstInfo flags{};
            flags.type.Assign(TextureType::Buffer);
            flags.descriptor_index.Assign(index);
            const auto fetch = ir.ImageFetch(ir.Imm32(0), ir.Imm32(2), {}, ir.Imm32(0),
                                             IR::U32{}, flags);
            // This is an emitter fixture with already-resolved descriptor metadata,
            // so provide the indexed instruction produced by TexturePass.
            fetch.InstRecursive()->ReplaceOpcode(IR::Opcode::ImageFetch);
            if (sparse) {
                static_cast<void>(ir.GetSparseFromOp(fetch));
            }
            if (query) {
                const auto dimensions = ir.ImageQueryDimension(ir.Imm32(0), ir.Imm32(0),
                                                               ir.Imm1(true), flags);
                dimensions.InstRecursive()->ReplaceOpcode(IR::Opcode::ImageQueryDimensions);
            }
        }
    }

    std::vector<u32> Emit(std::vector<UniformTexelType> types = {}) {
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        RuntimeInfo runtime{};
        runtime.uniform_texel_types = std::move(types);
        Backend::Bindings bindings{};
        return Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

std::vector<std::vector<u32>> Instructions(std::span<const u32> code, u32 opcode) {
    std::vector<std::vector<u32>> result;
    for (size_t offset = 5; offset < code.size();) {
        const u32 length = code[offset] >> 16;
        REQUIRE(length > 0);
        REQUIRE(offset + length <= code.size());
        if ((code[offset] & 0xffff) == opcode) {
            result.emplace_back(code.begin() + offset + 1, code.begin() + offset + length);
        }
        offset += length;
    }
    return result;
}

void CheckFetch(const std::vector<u32>& code, std::span<const UniformTexelType> types) {
    const auto images = Instructions(code, 25); // OpTypeImage
    const auto vectors = Instructions(code, 23); // OpTypeVector
    const auto floats = Instructions(code, 22); // OpTypeFloat
    const auto integers = Instructions(code, 21); // OpTypeInt
    const auto fetches = Instructions(code, 95); // OpImageFetch
    const auto loads = Instructions(code, 61); // OpLoad
    const auto bitcasts = Instructions(code, 124); // OpBitcast
    REQUIRE(fetches.size() == types.size());
    for (size_t index = 0; index < types.size(); ++index) {
        const auto& fetch = fetches[index];
        const auto vector = std::ranges::find_if(vectors, [&](const auto& value) {
            return value[0] == fetch[0];
        });
        REQUIRE(vector != vectors.end());
        REQUIRE(vector->size() == 3);
        CHECK((*vector)[2] == 4);
        const u32 scalar = (*vector)[1];
        const auto image_load = std::ranges::find_if(loads, [&](const auto& value) {
            return value[1] == fetch[2];
        });
        REQUIRE(image_load != loads.end());
        CHECK(std::ranges::any_of(images, [&](const auto& value) {
            return value[0] == (*image_load)[0] && value[1] == scalar && value[2] == 5 &&
                   value[6] == 1;
        }));
        if (types[index] == UniformTexelType::Float) {
            CHECK(std::ranges::any_of(floats, [&](const auto& value) {
                return value[0] == scalar && value[1] == 32;
            }));
        } else {
            CHECK(std::ranges::any_of(integers, [&](const auto& value) {
                return value[0] == scalar && value[1] == 32 &&
                       value[2] == (types[index] == UniformTexelType::Sint ? 1U : 0U);
            }));
            CHECK(std::ranges::any_of(bitcasts, [&](const auto& value) {
                if (value[2] != fetch[1]) {
                    return false;
                }
                const auto carrier = std::ranges::find_if(vectors, [&](const auto& type) {
                    return type[0] == value[0] && type[2] == 4;
                });
                return carrier != vectors.end() &&
                       std::ranges::any_of(floats, [&](const auto& type) {
                           return type[0] == (*carrier)[1] && type[1] == 32;
                       });
            }));
        }
    }
    CHECK(Instructions(code, 111).empty()); // No signed-to-float numeric conversion.
    CHECK(Instructions(code, 112).empty()); // No unsigned-to-float numeric conversion.
}
} // namespace

TEST_CASE("Uniform texel declarations independently match fetch numeric classes",
          "[uniform_texel_emitter]") {
    const std::vector types{UniformTexelType::Float, UniformTexelType::Uint,
                           UniformTexelType::Sint};
    FetchProgram fixture{3};
    CheckFetch(fixture.Emit(types), types);
}

TEST_CASE("Unspecialized uniform texels retain float fetches", "[uniform_texel_emitter]") {
    FetchProgram fixture{3};
    const std::array types{UniformTexelType::Float, UniformTexelType::Float,
                          UniformTexelType::Float};
    CheckFetch(fixture.Emit(), types);
}

TEST_CASE("Uniform texel specialization rejects incomplete or unsupported declarations",
          "[uniform_texel_emitter]") {
    SECTION("Missing specialization entry") {
        FetchProgram fixture{2};
        CHECK_THROWS_AS(fixture.Emit({UniformTexelType::Uint}), LogicError);
    }
    SECTION("Extra specialization entry") {
        FetchProgram fixture{1};
        CHECK_THROWS_AS(fixture.Emit({UniformTexelType::Uint, UniformTexelType::Sint}), LogicError);
    }
    SECTION("Invalid numeric enum") {
        FetchProgram fixture{1};
        CHECK_THROWS_AS(fixture.Emit({static_cast<UniformTexelType>(255)}), LogicError);
    }
    SECTION("Arrays remain unsupported") {
        FetchProgram fixture{1};
        fixture.program.info.texture_buffer_descriptors.front().count = 2;
        CHECK_THROWS_AS(fixture.Emit({UniformTexelType::Uint}), NotImplementedException);
    }
}

TEST_CASE("Uniform texel dimension queries retain their integer result contract",
          "[uniform_texel_emitter]") {
    for (const auto type : {UniformTexelType::Float, UniformTexelType::Uint,
                            UniformTexelType::Sint}) {
        FetchProgram fixture{1, false, true};
        const auto code = fixture.Emit({type});
        CheckFetch(code, std::array{type});
        const auto sizes = Instructions(code, 104); // OpImageQuerySize
        const auto integers = Instructions(code, 21);
        REQUIRE(sizes.size() == 1);
        CHECK(std::ranges::any_of(integers, [&](const auto& value) {
            return value[0] == sizes[0][0] && value[1] == 32 && value[2] == 0;
        }));
        CHECK(Instructions(code, 106).empty()); // No mip query for a buffer.
    }
}
