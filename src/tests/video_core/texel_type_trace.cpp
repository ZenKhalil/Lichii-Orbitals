// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/texel_type_trace_capture.h"

namespace {
using namespace Vulkan::TexelTypeTrace;

Row MakeRow(Kind kind, Numeric selected = Numeric::Uint) {
    Row row{};
    row.kind = kind;
    row.compiled = Numeric::Float;
    row.selected_class = selected;
    return row;
}
} // namespace

TEST_CASE("Texel metadata trace has exact opt-in and bounded independent budgets",
          "[video_core][texel_type_trace]") {
    CHECK(IsEnabledValue("1"));
    for (const char* value :
         std::array<const char*, 8>{nullptr, "", "0", "01", "true", "1 ", " 1", "11"}) {
        CHECK_FALSE(IsEnabledValue(value));
    }
    CHECK(Classify(false, false, false) == Numeric::Unknown);
    CHECK(Classify(true, false, false) == Numeric::Float);
    CHECK(Classify(true, true, false) == Numeric::Uint);
    CHECK(Classify(true, true, true) == Numeric::Sint);
    CHECK_FALSE(Mismatch(Numeric::Float, Numeric::Unknown));
    CHECK(Mismatch(Numeric::Uint, Numeric::Sint));
    CHECK_FALSE(Mismatch(Numeric::Uint, Numeric::Uint));

    Budget local;
    // Consume compute first: graphics must retain both kinds and both quota classes.
    for (const auto stage : {5U, 0U}) {
        for (const auto kind : {Kind::Uniform, Kind::Storage}) {
            for (const bool mismatch : {false, true}) {
                Row row = MakeRow(kind);
                row.stage = stage;
                row.mismatch = mismatch;
                const auto limit = mismatch ? Budget::MismatchLimit : Budget::ControlLimit;
                for (std::uint32_t i = 0; i < limit; ++i) {
                    REQUIRE(local.Reserve(row));
                    CHECK(row.ticket == i + 1);
                    CHECK(row.limit == limit);
                }
                CHECK_FALSE(local.Reserve(row));
            }
        }
    }
    CHECK_FALSE(local.Available());
}

TEST_CASE("Texel metadata capture owns immutable bounded rows and restores nested scopes",
          "[video_core][texel_type_trace]") {
    Budget local;
    int queue{}, other_queue{};
    CHECK(current == nullptr);
    {
        Scope disabled{&queue, true, false, local};
        disabled.Add(MakeRow(Kind::Uniform));
        CHECK_FALSE(disabled.Active());
        CHECK_FALSE(Scope::SealCurrent());
        CHECK(local.counts[1].load() == 0);
    }
    std::shared_ptr<const Batch> sealed;
    {
        Scope outer{&queue, true, true, local};
        auto row = MakeRow(Kind::Uniform);
        row.entry = 7;
        outer.Add(row);
        {
            Scope unsupported{&other_queue, false, true, local};
            Scope::Host(&queue, 7, 12, 24, 48, 99);
        }
        Scope::Host(&other_queue, 7, 12, 24, 48, 99);
        Scope::Host(&queue, 8, 12, 24, 48, 99);
        Scope::Host(&queue, 7, 123, 256, 1024, 98);
        for (std::size_t i = 1; i <= Batch::RowLimit; ++i) {
            row.entry = 7 + i;
            outer.Add(row);
        }
        sealed = Scope::SealCurrent();
        REQUIRE(sealed);
        CHECK(sealed->size == Batch::RowLimit);
        CHECK(sealed->row_cap_hits == 1);
        CHECK(sealed->rows[0].host_observed);
        CHECK(sealed->rows[0].buffer_view == 123);
        CHECK(sealed->rows[0].host_offset == 256);
        CHECK(sealed->rows[0].host_size == 1024);
        CHECK(sealed->rows[0].vk_format == 98);
        CHECK_FALSE(sealed->rows[1].host_observed);
        Scope::Host(&queue, 7, 999, 1, 2, 3);
        outer.Add(row);
        CHECK(sealed->rows[0].buffer_view == 123);
        CHECK_FALSE(outer.Active());
    }
    CHECK(current == nullptr);
    CHECK(local.abandoned_batches.load() == 0);
    CHECK(sealed->ClaimReport());
    CHECK_FALSE(sealed->ClaimReport());
    try {
        Scope failing{&queue, true, true, local};
        failing.Add(MakeRow(Kind::Storage));
        throw std::runtime_error("test unwind");
    } catch (const std::runtime_error&) {
        // The scope must restore TLS during ordinary exception unwinding.
    }
    CHECK(current == nullptr);
    CHECK(local.abandoned_batches.load() == 1);
    std::weak_ptr<const Batch> dropped;
    {
        Scope deferred{&queue, true, true, local};
        deferred.Add(MakeRow(Kind::Storage));
        auto pending = Scope::SealCurrent();
        dropped = pending;
        REQUIRE(pending);
        CHECK_FALSE(dropped.expired());
    }
    CHECK(dropped.expired());
    CHECK(local.abandoned_batches.load() == 1); // Counts only abandonment before sealing.
}

TEST_CASE("Texel metadata global quotas do not overgrant across capture threads",
          "[video_core][texel_type_trace]") {
    Budget local;
    std::array<std::atomic<unsigned>, 8> granted{};
    std::array<std::thread, 8> workers;
    for (auto& worker : workers) {
        worker = std::thread([&] {
            std::size_t bucket{};
            for (const auto stage : {5U, 0U}) {
                for (const auto kind : {Kind::Uniform, Kind::Storage}) {
                    for (const bool mismatch : {false, true}) {
                        Row row = MakeRow(kind);
                        row.stage = stage;
                        row.mismatch = mismatch;
                        for (unsigned i = 0; i < 32; ++i) {
                            if (local.Reserve(row)) {
                                ++granted[bucket];
                            }
                        }
                        ++bucket;
                    }
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (std::size_t bucket = 0; bucket < granted.size(); ++bucket) {
        CHECK(granted[bucket].load() ==
              (bucket % 2 ? Budget::MismatchLimit : Budget::ControlLimit));
    }
    CHECK_FALSE(local.Available());
}

TEST_CASE("Texel metadata follows actual declaration and payload indexing",
          "[video_core][texel_type_trace]") {
    Shader::Info previous;
    previous.constant_buffer_descriptors.push_back({.index = 0, .count = 2});
    previous.storage_buffers_descriptors.push_back({.count = 3});
    previous.texture_buffer_descriptors.push_back({.count = 4});
    previous.image_buffer_descriptors.push_back({.count = 2});
    previous.texture_descriptors.push_back({.count = 5});
    previous.image_descriptors.push_back({.count = 6});
    Shader::Info info;
    info.constant_buffer_descriptors.push_back({.index = 0, .count = 1});
    info.storage_buffers_descriptors.push_back({.count = 2});
    info.uses_atomic_image_u32 = true;
    Offsets offsets;
    offsets.SkipStage(previous);
    offsets.SkipBuffers(info);
    CHECK(offsets.binding == 8);
    CHECK(offsets.entry == 25);
    Budget local;
    int queue{};
    Scope scope{&queue, true, true, local};
    const Identity identity{.hash = 42, .program_address = 100, .stage = 5, .native = true};
    Shader::ImageBufferDescriptor storage{
        .format = Shader::ImageFormat::Typeless,
        .is_written = true,
        .is_read = true,
        .is_integer = true,
        .cbuf_index = 3,
        .cbuf_offset = 24,
        .count = 3,
        .size_shift = 2,
    };
    using VideoCore::Surface::PixelFormat;
    Capture(scope, info, identity, offsets, storage, 2, 64, 2, PixelFormat::R32_SINT,
            PixelFormat::R32_SINT, 4096, 256, 0);
    ++offsets.binding;
    offsets.entry += storage.count;
    Shader::TextureBufferDescriptor sampled{};
    sampled.count = 1;
    sampled.has_secondary = true;
    sampled.secondary_cbuf_index = 4;
    Capture(scope, info, identity, offsets, sampled, 0, 96, 3, PixelFormat::R32_UINT,
            PixelFormat::R32_UINT, 8192, 512, 0);
    sampled.handle_program = 6;
    Capture(scope, info, identity, offsets, sampled, 0, 96, 3, PixelFormat::Invalid,
            PixelFormat::Invalid, 0, 0, 0);
    const auto batch = Scope::SealCurrent();
    REQUIRE(batch);
    REQUIRE(batch->size == 3);
    const auto& first = batch->rows[0];
    CHECK(first.entry == 27);
    CHECK(first.binding == 8);
    CHECK(first.element == 2);
    CHECK(first.compiled == Numeric::Uint);
    CHECK(first.selected_class == Numeric::Sint);
    CHECK(first.mismatch);
    CHECK(first.read);
    CHECK(first.written);
    CHECK(first.stage_image_atomic);
    CHECK(first.shader_hash == 42);
    CHECK(first.program_address == 100);
    CHECK(first.native);
    CHECK(first.cbuf == 3);
    CHECK(first.cbuf_offset == 24);
    CHECK(first.handle_source == HandleSource::ConstantBuffer);
    const auto& second = batch->rows[1];
    CHECK(second.entry == 28);
    CHECK(second.binding == 9);
    CHECK(second.compiled == Numeric::Float);
    CHECK(second.handle_source == HandleSource::SecondaryConstantBuffer);
    CHECK_FALSE(second.written);
    CHECK(batch->rows[2].handle_source == HandleSource::Computed);
    CHECK_FALSE(batch->rows[2].mismatch);

    // Reproduce the coverage loss: repeated compute uniform mismatches exhaust
    // their quota before a later graphics pipeline reaches its first mismatch.
    // Use the actual capture adapter and bounded Scope batches, not raw counters.
    Budget families;
    Shader::Info uniform_info;
    Shader::TextureBufferDescriptor uniform{};
    uniform.count = 1;
    const Offsets uniform_offsets{.binding = 11, .entry = 25};
    const Identity compute{.hash = 43, .program_address = 200, .stage = 5, .native = true};
    for (std::uint32_t start = 0; start < Budget::MismatchLimit; start += Batch::RowLimit) {
        Scope compute_scope{&queue, true, true, families};
        for (std::size_t i = 0; i < Batch::RowLimit; ++i) {
            Capture(compute_scope, uniform_info, compute, uniform_offsets, uniform, 0, 96, 3,
                    PixelFormat::R32_UINT, PixelFormat::R32_UINT, 8192, 512, 0);
        }
        const auto compute_batch = Scope::SealCurrent();
        REQUIRE(compute_batch);
        REQUIRE(compute_batch->size == Batch::RowLimit);
        for (std::size_t i = 0; i < compute_batch->size; ++i) {
            CHECK(compute_batch->rows[i].stage == 5);
            CHECK(compute_batch->rows[i].ticket == start + i + 1);
        }
    }
    {
        Scope exhausted_compute{&queue, true, true, families};
        Capture(exhausted_compute, uniform_info, compute, uniform_offsets, uniform, 0, 96, 3,
                PixelFormat::R32_UINT, PixelFormat::R32_UINT, 8192, 512, 0);
        CHECK_FALSE(Scope::SealCurrent());
    }
    {
        Scope graphics_scope{&queue, true, true, families};
        for (std::uint32_t stage = 0; stage < 5; ++stage) {
            const Identity graphics{.hash = 44, .program_address = 300,
                                    .stage = stage, .native = true};
            Capture(graphics_scope, uniform_info, graphics, uniform_offsets, uniform, 0, 96, 3,
                    PixelFormat::R32_UINT, PixelFormat::R32_UINT, 8192, 512, 0);
        }
        Scope::Host(&queue, 25, 123, 256, 1024, 98);
        const auto graphics_batch = Scope::SealCurrent();
        REQUIRE(graphics_batch);
        REQUIRE(graphics_batch->size == 5);
        for (std::size_t i = 0; i < graphics_batch->size; ++i) {
            const auto& row = graphics_batch->rows[i];
            CHECK(row.stage == i);
            CHECK(row.kind == Kind::Uniform);
            CHECK(row.mismatch);
            CHECK(row.ticket == i + 1); // All graphics stages share one family quota.
            CHECK(row.limit == Budget::MismatchLimit);
            CHECK(row.host_observed);
            CHECK(row.buffer_view == 123);
        }
    }
}

TEST_CASE("Uniform family assessment follows immutable bounded descriptor rows",
          "[video_core][texel_type_trace]") {
    Budget local;
    int queue{};
    Vulkan::UniformFamilyAssessment assessment;
    assessment.observed = true;
    assessment.assessed_stage = 0;
    assessment.policy_evaluated = true;
    assessment.reason = Vulkan::UniformFamilyReason::DeclarationCount;
    assessment.declaration_count = 2;
    std::shared_ptr<const Batch> sealed;
    {
        Scope scope{&queue, true, true, local};
        scope.SetUniformFamilyAssessment(&assessment);
        auto row = MakeRow(Kind::Uniform);
        row.stage = 5;
        scope.Add(row);
        sealed = Scope::SealCurrent();
        REQUIRE(sealed);
        assessment.reason = Vulkan::UniformFamilyReason::Eligible;
        scope.Add(row);
        CHECK(sealed->size == 1);
        CHECK(sealed->rows[0].uniform_family.reason ==
              Vulkan::UniformFamilyReason::DeclarationCount);
        CHECK(sealed->rows[0].uniform_family.declaration_count == 2);
        CHECK(sealed->rows[0].stage == 5);
        CHECK(sealed->rows[0].uniform_family.assessed_stage == 0);
        CHECK(sealed->rows[0].ticket == 1);
    }
    {
        Scope disabled{&queue, true, false, local};
        disabled.SetUniformFamilyAssessment(&assessment);
        disabled.Add(MakeRow(Kind::Uniform));
        CHECK_FALSE(Scope::SealCurrent());
    }
    {
        Scope unknown{&queue, true, true, local};
        unknown.Add(MakeRow(Kind::Uniform));
        const auto batch = Scope::SealCurrent();
        REQUIRE(batch);
        CHECK_FALSE(batch->rows[0].uniform_family.observed);
        CHECK(batch->rows[0].uniform_family.reason == Vulkan::UniformFamilyReason::Unknown);
    }
}


TEST_CASE("Texel shader filter strictly parses a bounded startup list",
          "[video_core][texel_type_trace]") {
    const auto absent = ParseShaderFilter(nullptr);
    CHECK_FALSE(absent.present);
    CHECK(absent.valid);
    CHECK(absent.Accepts(0));
    CHECK(absent.Accepts(~std::uint64_t{}));
    const auto single = ParseShaderFilter("5BF5fe86e1f82d9a");
    CHECK(single.valid);
    CHECK(single.count == 1);
    CHECK(single.Accepts(0x5bf5fe86e1f82d9a));
    CHECK_FALSE(single.Accepts(0x3183b83b38829382));
    const auto eight = ParseShaderFilter(
        "0000000000000000,0000000000000001,0000000000000002,0000000000000003,"
        "0000000000000004,0000000000000005,0000000000000006,ffffffffffffffff");
    CHECK(eight.valid);
    CHECK(eight.count == 8);
    CHECK(eight.Accepts(0));
    CHECK(eight.Accepts(~std::uint64_t{}));
    for (const char* value : {
             "", "1", "000000000000000", "00000000000000000", "0x00000000000000",
             "000000000000000g", " 0000000000000000", "0000000000000000 ",
             "0000000000000000,", ",0000000000000000", "0000000000000000,,0000000000000001",
             "0000000000000000,0000000000000000", "ffffffffffffffff,FFFFFFFFFFFFFFFF",
             "0000000000000000,0000000000000001,0000000000000002,0000000000000003,"
             "0000000000000004,0000000000000005,0000000000000006,0000000000000007,0000000000000008"}) {
        const auto invalid = ParseShaderFilter(value);
        CHECK(invalid.present);
        CHECK_FALSE(invalid.valid);
        CHECK_FALSE(invalid.Accepts(0));
        CHECK_FALSE(invalid.Accepts(0xffffffffffffffff));
    }
    // No terminator is needed beyond the bounded eighth separator. All fields are unique.
    std::array<char, 136> nonterminated{};
    for (std::size_t field = 0; field < 8; ++field) {
        for (std::size_t digit = 0; digit < 16; ++digit) {
            nonterminated[field * 17 + digit] = digit == 15 ? static_cast<char>('0' + field) : '0';
        }
        nonterminated[field * 17 + 16] = ',';
    }
    CHECK_FALSE(ParseShaderFilter(nonterminated.data()).valid);
}

TEST_CASE("Texel shader filter preserves target tickets before earlier controls",
          "[video_core][texel_type_trace]") {
    using VideoCore::Surface::PixelFormat;
    const auto filter = ParseShaderFilter("5bf5fe86e1f82d9a");
    const auto unrestricted = ParseShaderFilter(nullptr);
    const auto invalid = ParseShaderFilter("");
    constexpr std::uint64_t early_hash = 0x3183b83b38829382;
    constexpr std::uint64_t target_hash = 0x5bf5fe86e1f82d9a;
    int queue{};
    Shader::Info info;
    Shader::ImageBufferDescriptor desc{.format = Shader::ImageFormat::Typeless,
                                      .is_written = true, .count = 1};
    const Offsets offsets{.binding = 2, .entry = 3};
    const auto capture = [&](Scope& scope, std::uint64_t hash) {
        Capture(scope, info, Identity{.hash = hash, .stage = 5, .native = true}, offsets, desc,
                0, 1, 1, PixelFormat::R32_FLOAT, PixelFormat::R32_FLOAT, 4096, 256, 0);
    };
    Budget filtered;
    {
        Scope scope{&queue, true, true, filtered, &filter};
        for (std::uint32_t i = 0; i < 64; ++i) {
            capture(scope, early_hash);
        }
        for (const auto& count : filtered.counts) {
            CHECK(count.load() == 0);
        }
        CHECK_FALSE(Scope::SealCurrent());
    }
    {
        Scope scope{&queue, true, true, filtered, &filter};
        for (std::uint32_t i = 0; i < Budget::ControlLimit + 1; ++i) {
            capture(scope, target_hash);
        }
        Scope::Host(&queue, 3, 99, 128, 256, 100);
        const auto batch = Scope::SealCurrent();
        REQUIRE(batch);
        REQUIRE(batch->size == Budget::ControlLimit);
        for (std::size_t i = 0; i < batch->size; ++i) {
            CHECK(batch->rows[i].shader_hash == target_hash);
            CHECK(batch->rows[i].ticket == i + 1);
            CHECK(batch->rows[i].shader_filter_hashes == 1);
            CHECK(batch->rows[i].host_observed);
            CHECK(batch->rows[i].buffer_view == 99);
            CHECK(batch->rows[i].host_offset == 128);
            CHECK(batch->rows[i].host_size == 256);
            CHECK(batch->rows[i].compiled == Numeric::Float);
        }
    }
    Budget old_behavior;
    {
        Scope scope{&queue, true, true, old_behavior, &unrestricted};
        for (std::uint32_t i = 0; i < Budget::ControlLimit; ++i) {
            capture(scope, early_hash);
        }
        capture(scope, target_hash);
        const auto batch = Scope::SealCurrent();
        REQUIRE(batch);
        CHECK(batch->size == Budget::ControlLimit);
        CHECK(batch->rows[0].shader_hash == early_hash);
        CHECK(batch->rows[0].shader_filter_hashes == 0);
    }
    for (const bool enabled : {false, true}) {
        Budget rejected;
        Scope scope{&queue, true, enabled, rejected, &invalid};
        capture(scope, target_hash);
        CHECK_FALSE(Scope::SealCurrent());
        for (const auto& count : rejected.counts) {
            CHECK(count.load() == 0);
        }
    }
    Budget full;
    {
        Scope scope{&queue, true, true, full, &filter};
        auto mismatch = MakeRow(Kind::Storage);
        mismatch.stage = 5;
        mismatch.shader_hash = target_hash;
        for (std::size_t i = 0; i < Batch::RowLimit; ++i) {
            scope.Add(mismatch);
        }
        mismatch.shader_hash = early_hash;
        scope.Add(mismatch);
        const auto batch = Scope::SealCurrent();
        REQUIRE(batch);
        CHECK(batch->size == Batch::RowLimit);
        CHECK(batch->row_cap_hits == 0);
    }
}
