// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/control/channel_state.h"
#include "video_core/dirty_flags.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2_debug_control.h"
#include "video_core/native_shader_source.h"
#include "video_core/shader_cache.h"

namespace VideoCommon {
struct ShaderCacheTestAccess {
    static std::unique_ptr<ShaderCache> Create(Tegra::MaxwellDeviceMemoryManager& memory) {
        return std::unique_ptr<ShaderCache>{new ShaderCache{memory}};
    }
    static void Register(ShaderCache& cache, VAddr address, size_t size, u64 hash) {
        auto data = std::make_unique<ShaderInfo>();
        data->unique_hash = hash;
        cache.Register(std::move(data), address, size);
    }
    static const ShaderInfo* Find(const ShaderCache& cache, VAddr address) {
        return cache.TryGet(address);
    }
    static void RegisterNative(ShaderCache& cache, VAddr address, u64 hash, u64 key) {
        auto data = std::make_unique<ShaderInfo>();
        data->unique_hash = hash;
        auto lowered = std::make_shared<LoweredNativeCode>();
        lowered->native_program_key = key;
        data->lowered_native = std::move(lowered);
        cache.Register(std::move(data), address, 0x100);
    }
    static void Reject(ShaderCache& cache, VAddr address,
                       NativeShaderSourceRanges ranges = {}, std::string message = "rejected") {
        try {
            throw NativeShaderSourceError{Shader::Exception{std::move(message)}, std::move(ranges)};
        } catch (...) {
            cache.RememberRejection(address);
        }
    }
    static std::string RejectionMessage(ShaderCache& cache, VAddr address) {
        try {
            cache.RethrowIfRejected(address);
        } catch (const std::exception& error) {
            return error.what();
        }
        return {};
    }
    static bool RefreshStages(ShaderCache& cache, std::array<u64, 6>& hashes) {
        return cache.RefreshStages(hashes);
    }
    static bool IsRejected(ShaderCache& cache, VAddr address) {
        return cache.IsRejected(address);
    }
};
} // namespace VideoCommon

using VideoCommon::ShaderCacheTestAccess;

TEST_CASE("Shader batch invalidation matches sequential cache contents", "[shader_invalidation]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto batched = ShaderCacheTestAccess::Create(device_memory);
    auto sequential = ShaderCacheTestAccess::Create(device_memory);
    std::mt19937 random{0x16'67};
    constexpr VAddr base = 0x100000;
    for (unsigned iteration = 0; iteration < 600; ++iteration) {
        for (unsigned slot = 0; slot < 128; ++slot) {
            const auto address = base + slot * 4096;
            if (!ShaderCacheTestAccess::Find(*batched, address)) {
                const auto size = 1 + random() % 65536;
                ShaderCacheTestAccess::Register(*batched, address, size, iteration + slot);
                ShaderCacheTestAccess::Register(*sequential, address, size, iteration + slot);
            }
        }
        std::vector<std::pair<DAddr, size_t>> writes;
        for (unsigned i = 0, count = random() % 24; i < count; ++i) {
            writes.emplace_back(base + random() % (128 * 4096), random() % 32768);
            if (i % 4 == 0) {
                writes.push_back(writes.back());
            }
        }
        for (const auto& [address, size] : writes) {
            sequential->InvalidateRegion(address, size);
        }
        batched->InvalidateRegions(writes);
        for (unsigned slot = 0; slot < 128; ++slot) {
            const auto address = base + slot * 4096;
            const auto actual = ShaderCacheTestAccess::Find(*batched, address);
            const auto expected = ShaderCacheTestAccess::Find(*sequential, address);
            REQUIRE(bool(actual) == bool(expected));
            if (actual) {
                REQUIRE(actual->unique_hash == expected->unique_hash);
            }
        }
    }
}

TEST_CASE("Shader empty batches preserve pending invalidations", "[shader_invalidation]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto cache = ShaderCacheTestAccess::Create(device_memory);
    constexpr VAddr address = 0x100000;
    ShaderCacheTestAccess::Register(*cache, address, 32768, 123);
    cache->OnCacheInvalidation(address, 1);
    cache->InvalidateRegions({});
    REQUIRE(ShaderCacheTestAccess::Find(*cache, address));
    const std::array unrelated{std::pair<DAddr, size_t>{address + 0x100000, 4096}};
    cache->InvalidateRegions(unrelated);
    REQUIRE_FALSE(ShaderCacheTestAccess::Find(*cache, address));
}

TEST_CASE("Debug server rejections drop matching native programs", "[shader_invalidation]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto cache = ShaderCacheTestAccess::Create(device_memory);
    constexpr VAddr by_key = 0x100000;
    constexpr VAddr by_hash = 0x200000;
    constexpr VAddr kept = 0x300000;
    constexpr VAddr maxwell = 0x400000;
    constexpr VAddr earlier = 0x500000;
    ShaderCacheTestAccess::RegisterNative(*cache, by_key, 0x11, 0xaaaa);
    ShaderCacheTestAccess::RegisterNative(*cache, by_hash, 0x22, 0xbbbb);
    ShaderCacheTestAccess::RegisterNative(*cache, kept, 0x33, 0xcccc);
    ShaderCacheTestAccess::Register(*cache, maxwell, 0x100, 0xaaaa);
    ShaderCacheTestAccess::Reject(*cache, earlier);

    VideoCore::Nx2Debug::BeginSession();
    const std::array<u64, 2> rejected{0xaaaa, 0x22};
    const u64 generation = VideoCore::Nx2Debug::SetRejected(rejected, true);
    const auto change = VideoCore::Nx2Debug::TakeRejectChange();
    REQUIRE(change);
    REQUIRE(change->generation == generation);
    REQUIRE_FALSE(change->unrejected);
    cache->RefreshDebugRejections(change->unrejected);
    REQUIRE_FALSE(ShaderCacheTestAccess::Find(*cache, by_key));
    REQUIRE_FALSE(ShaderCacheTestAccess::Find(*cache, by_hash));
    REQUIRE(ShaderCacheTestAccess::Find(*cache, kept));
    // Only lowered native programs are matched; a Maxwell program with the same hash stays.
    REQUIRE(ShaderCacheTestAccess::Find(*cache, maxwell));
    REQUIRE(ShaderCacheTestAccess::IsRejected(*cache, earlier));

    const std::array<u64, 1> restored{0xaaaa};
    VideoCore::Nx2Debug::SetRejected(restored, false);
    const auto undo = VideoCore::Nx2Debug::TakeRejectChange();
    REQUIRE(undo);
    REQUIRE(undo->unrejected);
    cache->RefreshDebugRejections(undo->unrejected);
    REQUIRE_FALSE(ShaderCacheTestAccess::IsRejected(*cache, earlier));
    REQUIRE(VideoCore::Nx2Debug::RejectedHashes() == std::vector<u64>{0x22});
    VideoCore::Nx2Debug::EndSession();
}

TEST_CASE("Shader invalidation CPU benchmark", "[.][cache_bench]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto batched = ShaderCacheTestAccess::Create(device_memory);
    auto sequential = ShaderCacheTestAccess::Create(device_memory);
    std::vector<std::pair<DAddr, size_t>> writes;
    constexpr VAddr base = 0x100000;
    for (unsigned slot = 0; slot < 768; ++slot) {
        writes.emplace_back(base + slot * 65536, 1);
    }
    double sequential_ms{}, batched_ms{};
    for (unsigned repeat = 0; repeat < 20; ++repeat) {
        for (unsigned slot = 0; slot < 1024; ++slot) {
            const auto address = base + slot * 65536;
            if (!ShaderCacheTestAccess::Find(*batched, address)) {
                ShaderCacheTestAccess::Register(*batched, address, 64, slot);
                ShaderCacheTestAccess::Register(*sequential, address, 64, slot);
            }
        }
        auto start = std::chrono::steady_clock::now();
        for (const auto& [address, size] : writes) {
            sequential->InvalidateRegion(address, size);
        }
        sequential_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        start = std::chrono::steady_clock::now();
        batched->InvalidateRegions(writes);
        batched_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        REQUIRE_FALSE(ShaderCacheTestAccess::Find(*batched, base));
        REQUIRE(ShaderCacheTestAccess::Find(*batched, base + 1023 * 65536));
    }
    std::cout << "Shader invalidation: sequential=" << sequential_ms << " ms batched=" << batched_ms
              << " ms (CPU only, not gameplay FPS)\n";
}


TEST_CASE("Rejected native shader body writes permit another compile", "[shader_invalidation][shader_rejection]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto cache = ShaderCacheTestAccess::Create(device_memory);
    constexpr DAddr start = 0x100300;
    const VideoCommon::NativeShaderSourceRanges pages{{0x100000, 0x2000}, {0x900000, 0x1000}};
    ShaderCacheTestAccess::Reject(*cache, start, pages);
    REQUIRE(ShaderCacheTestAccess::IsRejected(*cache, start));
    REQUIRE(ShaderCacheTestAccess::RejectionMessage(*cache, start) == "rejected");
    DAddr write{};
    SECTION("later bytes on the first page") { write = start + 0x780; }
    SECTION("a later contiguous source page") { write = 0x101700; }
    SECTION("a GPU-contiguous page with separate device backing") { write = 0x900040; }
    cache->InvalidateRegion(write, 16);
    REQUIRE_FALSE(ShaderCacheTestAccess::IsRejected(*cache, start));
    REQUIRE(ShaderCacheTestAccess::RejectionMessage(*cache, start).empty());
    // A stable failure can be cached again after retry; unrelated writes do not cause churn.
    ShaderCacheTestAccess::Reject(*cache, start, pages, "retry failed");
    cache->InvalidateRegion(0x500000, 16);
    REQUIRE(ShaderCacheTestAccess::RejectionMessage(*cache, start) == "retry failed");
}

TEST_CASE("Rejected native shader source pages reach the invalidation filter", "[shader_invalidation][shader_rejection]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto cache = ShaderCacheTestAccess::Create(device_memory);
    ShaderCacheTestAccess::Reject(*cache, 0x100300, {{0x100000, 0x2000}, {0x900000, 0x1000}});
    REQUIRE(cache->page_filter.Any(0x101700, 16));
    REQUIRE(cache->page_filter.Any(0x900040, 16));
    REQUIRE_FALSE(cache->page_filter.Any(0x500000, 16));
}

TEST_CASE("Rejected native shader batch invalidation preserves unrelated failures", "[shader_invalidation][shader_rejection]") {
    Core::DeviceMemory memory;
    Tegra::MaxwellDeviceMemoryManager device_memory{memory};
    auto cache = ShaderCacheTestAccess::Create(device_memory);
    constexpr DAddr changed = 0x100300, kept = 0x300100;
    ShaderCacheTestAccess::Reject(*cache, changed, {{0x100000, 0x2000}}, "first error");
    ShaderCacheTestAccess::Reject(*cache, changed, {{0x700000, 0x1000}}, "duplicate error");
    ShaderCacheTestAccess::Reject(*cache, kept, {{0x300000, 0x1000}}, "kept error");
    cache->InvalidateRegion(0x102000, 1); // Half-open end is outside the recorded range.
    cache->InvalidateRegion(0x101700, 0); // An empty write changes nothing.
    cache->InvalidateRegion(0x700000, 16); // Duplicate rejection did not extend ownership.
    REQUIRE(ShaderCacheTestAccess::RejectionMessage(*cache, changed) == "first error");
    const std::array writes{std::pair<DAddr, size_t>{0x101700, 16},
                            std::pair<DAddr, size_t>{0x101700, 16},
                            std::pair<DAddr, size_t>{0x500000, 16}};
    cache->InvalidateRegions(writes);
    REQUIRE_FALSE(ShaderCacheTestAccess::IsRejected(*cache, changed));
    REQUIRE(ShaderCacheTestAccess::RejectionMessage(*cache, kept) == "kept error");
    cache->RefreshDebugRejections(true);
    REQUIRE_FALSE(ShaderCacheTestAccess::IsRejected(*cache, kept));
}

TEST_CASE("Native source pages preserve discontiguous backing without broad ranges", "[shader_invalidation][shader_rejection]") {
    const std::array ranges{std::pair<GPUVAddr, size_t>{0x80ff0, 0x1020},
                            std::pair<GPUVAddr, size_t>{0x82000, 16}};
    const auto result = VideoCommon::NativeShaderSourcePages(ranges, [](GPUVAddr page) -> std::optional<DAddr> {
        if (page == 0x80000) return 0x300000;
        if (page == 0x81000) return 0x900000;
        if (page == 0x82000) return 0x301000;
        return std::nullopt;
    });
    const VideoCommon::NativeShaderSourceRanges expected{{0x300000, 0x2000}, {0x900000, 0x1000}};
    REQUIRE(result == expected);
    const std::array too_large{std::pair<GPUVAddr, size_t>{0x80000, 2 * 1024 * 1024}};
    unsigned calls{};
    REQUIRE(VideoCommon::NativeShaderSourcePages(too_large, [&](GPUVAddr page) -> std::optional<DAddr> {
        ++calls;
        return page;
    }).empty());
    REQUIRE(calls == 0);
}


namespace {
// Exercise the real device-page counters through their observable CPU page-cache transitions.
// A device mapping without CPU backing would skip those counters and hide ownership leaks.
struct RejectedPageFixture {
    ScopedSystemTestContext context;
    Core::System system;
    std::unique_ptr<Kernel::KProcess> process;
    std::unique_ptr<Tegra::MaxwellDeviceMemoryManager> device;
    std::unique_ptr<VideoCommon::ShaderCache> cache;
    Core::Asid asid{};
    static constexpr DAddr Device = 0x100000;
    static constexpr VAddr Cpu = 0x4000;

    RejectedPageFixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(20, 12);
        auto* backing = system.DeviceMemory().buffer.BackingBasePointer() + 0x10000;
        for (size_t index = 0; index < 3; ++index) {
            table.entries.GetAndFault((Cpu >> 12) + index).Store(
                false, Common::PageType::Memory, 1,
                reinterpret_cast<uintptr_t>(backing) - Cpu);
        }
        auto& memory = process->GetMemory();
        memory.SetCurrentPageTable(*process);
        device = std::make_unique<Tegra::MaxwellDeviceMemoryManager>(system.DeviceMemory());
        asid = device->RegisterProcess(&memory);
        device->Map(Device, Cpu, 0x3000, asid);
        cache = ShaderCacheTestAccess::Create(*device);
    }
    ~RejectedPageFixture() {
        cache.reset();
        device->UnregisterProcess(asid);
        device.reset();
        system.Kernel().UnregisterKernelObject(process.get());
        process.reset();
    }
    Common::PageType PageType(size_t index) {
        return process->GetPageTable().GetImpl().entries.GetUnchecked((Cpu >> 12) + index).Type();
    }
};
} // namespace

TEST_CASE("Rejected shader page ownership balances mapped CPU protection", "[shader_invalidation][shader_rejection]") {
    RejectedPageFixture f;
    constexpr DAddr first = RejectedPageFixture::Device + 0x300;
    constexpr DAddr second = RejectedPageFixture::Device + 0x1100;
    ShaderCacheTestAccess::Reject(*f.cache, first, {{first & ~0xfffULL, 0x2000}});
    ShaderCacheTestAccess::Reject(*f.cache, second, {{second & ~0xfffULL, 0x2000}});
    // Duplicate rejection must not add a reference to an existing or a new page.
    ShaderCacheTestAccess::Reject(*f.cache, first, {{first & ~0xfffULL, 0x3000}});
    for (size_t page = 0; page < 3; ++page) {
        REQUIRE(f.PageType(page) == Common::PageType::RasterizerCachedMemory);
    }
    f.cache->InvalidateRegion(first + 0x780, 16);
    CHECK(f.PageType(0) == Common::PageType::Memory);
    CHECK(f.PageType(1) == Common::PageType::RasterizerCachedMemory);
    CHECK(f.PageType(2) == Common::PageType::RasterizerCachedMemory);
    SECTION("invalidation releases the final shared page reference") {
        f.cache->InvalidateRegion(second + 0x1000, 16);
    }
    SECTION("debug clear releases remaining references") {
        f.cache->RefreshDebugRejections(true);
    }
    SECTION("cache destruction releases remaining references") {
        f.cache.reset();
    }
    for (size_t page = 0; page < 3; ++page) {
        CHECK(f.PageType(page) == Common::PageType::Memory);
    }
}

TEST_CASE("Rejected shader invalidation defers channel flags to GPU refresh", "[shader_invalidation][shader_rejection]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::MemoryManager memory{system, device, 32, 1ULL << 31};
    Tegra::Control::ChannelState first{1}, second{2}, erased{3};
    auto cache = ShaderCacheTestAccess::Create(device);
    for (auto* channel : {&first, &second, &erased}) {
        channel->memory_manager = &memory;
        channel->payload.emplace(system, memory, *channel);
        cache->CreateChannel(*channel);
        channel->payload->maxwell_3d.dirty.flags.reset();
    }
    cache->EraseChannel(erased.bind_id);
    cache->BindToChannel(first.bind_id);
    ShaderCacheTestAccess::Reject(*cache, 0x100300, {{0x100000, 0x2000}});
    cache->InvalidateRegion(0x101700, 16);
    // Invalidation is also called by CPU threads. It must not write any engine's flags here.
    for (auto* channel : {&first, &second, &erased}) {
        CHECK_FALSE(channel->payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders]);
    }
    std::array<u64, 6> hashes{};
    ShaderCacheTestAccess::RefreshStages(*cache, hashes);
    // First channel has consumed its flag; every other active channel still needs refresh.
    CHECK_FALSE(first.payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders]);
    CHECK(second.payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders]);
    CHECK_FALSE(erased.payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders]);
    second.payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders] = false;
    ShaderCacheTestAccess::RefreshStages(*cache, hashes);
    CHECK_FALSE(second.payload->maxwell_3d.dirty.flags[VideoCommon::Dirty::Shaders]);
}
