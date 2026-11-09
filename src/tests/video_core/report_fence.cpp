// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/fence_manager.h"
#include "video_core/query_cache/query_cache.h"
#include "video_core/rasterizer_interface.h"

namespace {
class ReportRasterizer final : public VideoCore::RasterizerInterface {
public:
    void Draw(bool, u32) override {}
    void DrawTexture() override {}
    void Clear(u32) override {}
    void DispatchCompute() override {}
    void ResetCounter(VideoCommon::QueryType) override {}
    void Query(GPUVAddr, VideoCommon::QueryType, VideoCommon::QueryPropertiesFlags, u32,
               u32) override {}
    void BindGraphicsUniformBuffer(size_t, u32, GPUVAddr, u32) override {}
    void DisableGraphicsUniformBuffer(size_t, u32) override {}
    std::function<void(std::function<void()>&&)> fence;
    std::function<void(std::function<void()>&&)> sync;
    void SignalFence(std::function<void()>&& op) override { fence(std::move(op)); }
    void SyncOperation(std::function<void()>&& op) override { sync(std::move(op)); }
    void SignalSyncPoint(u32) override {}
    void SignalReference() override {}
    void ReleaseFences(bool) override {}
    void FlushAll() override {}
    void FlushRegion(DAddr, u64, VideoCommon::CacheType) override {}
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override {
        return false;
    }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override {
        return {addr, addr + size, false};
    }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(DAddr, u64) override {}
    bool OnCPUWrite(DAddr, u64) override {
        return false;
    }
    void InvalidateGPUCache() override {}
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t, GPUVAddr, u64) override {}
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override {
        std::terminate();
    }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}
};


struct Runtime;
struct QueryTraits { using RuntimeType = Runtime; };

class GuestStreamer final : public VideoCommon::GuestStreamer<QueryTraits> {
public:
    explicit GuestStreamer(Runtime& runtime) : VideoCommon::GuestStreamer<QueryTraits>{0, runtime} {}
    std::atomic<u32> freed{};
    void Free(size_t id) override {
        ++freed;
        VideoCommon::GuestStreamer<QueryTraits>::Free(id);
    }
};

struct Runtime {
    GuestStreamer streamer{*this};
    u32 synchronized{};
    VideoCommon::StreamerInterface* GetStreamerInterface(VideoCommon::QueryType type) {
        return type == VideoCommon::QueryType::Payload ? &streamer : nullptr;
    }
    void Bind3DEngine(Tegra::Engines::Maxwell3D*) {}
    void Barriers(bool) {}
    template <class T> void SyncValues(std::span<T> values) { synchronized += values.size(); }
    template <class T> void SyncValues(const std::vector<T>& values) { synchronized += values.size(); }
};

class QueryCache final : public VideoCommon::QueryCacheBase<QueryTraits> {
public:
    QueryCache(Tegra::GPU& gpu, ReportRasterizer& rasterizer,
               Tegra::MaxwellDeviceMemoryManager& device, Runtime& runtime,
               Tegra::MemoryManager& memory)
        : VideoCommon::QueryCacheBase<QueryTraits>{gpu, rasterizer, device, runtime} {
        gpu_memory = &memory;
    }
    bool Registered(DAddr address) {
        bool result = false;
        IterateCache<false>(address, 4, [&](QueryLocation) { result = true; });
        return result;
    }
};

struct EmptyCache {
    std::mutex mutex;
    bool flush{};
    bool HasUncommittedFlushes() const { return flush; }
    bool HasAsyncFlushWork() const { return flush; }
    bool ShouldWaitAsyncFlushes() const { return false; }
    void AccumulateFlushes() {}
    void CommitAsyncFlushes() {}
    void PopAsyncFlushes() {}
};

struct Fence : VideoCommon::FenceBase {
    explicit Fence(bool stubbed) : FenceBase{stubbed} {}
};

template <bool Async> struct FenceTraits {
    using FenceType = std::shared_ptr<Fence>;
    using TextureCacheType = EmptyCache;
    using BufferCacheType = EmptyCache;
    using QueryCacheType = QueryCache;
    static constexpr bool HAS_ASYNC_CHECK = Async;
};

struct Completion {
    std::mutex mutex;
    std::condition_variable cv;
    bool complete{}, waiting{}, timed_out{};
    u32 callbacks{};
    void Complete() {
        std::scoped_lock lock{mutex};
        complete = true;
        cv.notify_all();
    }
};

template <bool Async>
class ReportFenceManager final : public VideoCommon::FenceManager<FenceTraits<Async>> {
    using Base = VideoCommon::FenceManager<FenceTraits<Async>>;
public:
    ReportFenceManager(ReportRasterizer& rasterizer, Tegra::GPU& gpu, EmptyCache& texture,
                       EmptyCache& buffer, QueryCache& query, Runtime& runtime_, Completion& state_)
        : Base{rasterizer, gpu, texture, buffer, query}, runtime{runtime_}, state{state_} {}
    u32 created{}, sync_before_queue{};
    bool last_stubbed{true};
protected:
    std::shared_ptr<Fence> CreateFence(bool stubbed) override {
        ++created;
        last_stubbed = stubbed;
        return std::make_shared<Fence>(stubbed);
    }
    void QueueFence(std::shared_ptr<Fence>&) override { sync_before_queue = runtime.synchronized; }
    bool IsFenceSignaled(std::shared_ptr<Fence>& fence) const override {
        std::scoped_lock lock{state.mutex};
        return fence->IsStubbed() || state.complete;
    }
    void WaitFence(std::shared_ptr<Fence>& fence) override {
        if (fence->IsStubbed()) return;
        std::unique_lock lock{state.mutex};
        state.waiting = true;
        state.cv.notify_all();
        if (!state.cv.wait_for(lock, std::chrono::seconds{2}, [&] { return state.complete; })) {
            state.timed_out = true;
        }
    }
private:
    Runtime& runtime;
    Completion& state;
};

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    std::unique_ptr<Kernel::KProcess> process;
    std::unique_ptr<Tegra::GPU> gpu;
    std::unique_ptr<Tegra::MaxwellDeviceMemoryManager> device;
    ReportRasterizer rasterizer;
    std::unique_ptr<Tegra::MemoryManager> memory;
    Runtime runtime;
    std::unique_ptr<QueryCache> query;
    Core::Asid asid{};
    static constexpr GPUVAddr Address = 0x40000000;
    static constexpr DAddr Device = 0x1000;
    u32* words{};
    const Settings::GpuAccuracy old_accuracy = Settings::values.current_gpu_accuracy;
    const Settings::GpuFenceBehavior old_fence = Settings::values.gpu_fence_behavior.GetValue();
    Fixture() {
        Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
        Settings::values.gpu_fence_behavior.SetValue(Settings::GpuFenceBehavior::Default);
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(20, 12);
        auto* page = system.DeviceMemory().buffer.BackingBasePointer() + 0x10000;
        std::memset(page, 0, 0x1000);
        table.entries.GetAndFault(4).Store(false, Common::PageType::Memory, 1,
                                         reinterpret_cast<uintptr_t>(page) - 0x4000);
        auto& cpu_memory = process->GetMemory();
        cpu_memory.SetCurrentPageTable(*process);
        device = std::make_unique<Tegra::MaxwellDeviceMemoryManager>(system.DeviceMemory());
        device->BindInterface(&rasterizer);
        asid = device->RegisterProcess(&cpu_memory);
        device->Map(Device, 0x4000, 0x1000, asid);
        words = reinterpret_cast<u32*>(device->GetPointer<u8>(Device));
        gpu = std::make_unique<Tegra::GPU>(system, false, false);
        memory = std::make_unique<Tegra::MemoryManager>(system, *device, 32, 1ULL << 31, 16);
        memory->BindRasterizer(&rasterizer);
        memory->Map(Address, Device, 0x1000, Tegra::PTEKind::PITCH, false);
        query = std::make_unique<QueryCache>(*gpu, rasterizer, *device, runtime, *memory);
    }
    ~Fixture() {
        query.reset();
        memory.reset();
        gpu.reset();
        device->UnregisterProcess(asid);
        device.reset();
        system.Kernel().UnregisterKernelObject(process.get());
        process.reset();
        Settings::values.current_gpu_accuracy = old_accuracy;
        Settings::values.gpu_fence_behavior.SetValue(old_fence);
    }
    void Report(u32 offset, u32 value, bool fence = true) {
        query->CounterReport(Address + offset, VideoCommon::QueryType::Payload,
                             fence ? VideoCommon::QueryPropertiesFlags::IsAFence
                                   : VideoCommon::QueryPropertiesFlags{}, value, 0);
    }
};

template <bool Async> void CheckCompletion() {
    Fixture f;
    EmptyCache texture, buffer;
    Completion state;
    ReportFenceManager<Async> manager{f.rasterizer, *f.gpu, texture, buffer, *f.query,
                                      f.runtime, state};
    f.rasterizer.sync = [&](std::function<void()>&& op) { manager.SyncOperation(std::move(op)); };
    f.rasterizer.fence = [&](std::function<void()>&& op) {
        manager.SignalFence([&, op = std::move(op)] {
            op();
            std::scoped_lock lock{state.mutex};
            ++state.callbacks;
            state.cv.notify_all();
        });
    };
    // The payload is known to the CPU, but it releases work not yet completed by the host GPU.
    REQUIRE_FALSE(f.query->HasUncommittedFlushes());
    f.Report(0, 0x12345678);
    CHECK(manager.created == 1);
    CHECK_FALSE(manager.last_stubbed);
    CHECK(manager.sync_before_queue == 1);
    if constexpr (Async) {
        std::unique_lock lock{state.mutex};
        CHECK(state.cv.wait_for(lock, std::chrono::seconds{2}, [&] {
            return state.waiting || state.callbacks != 0;
        }));
        CHECK(state.callbacks == 0);
    } else {
        manager.SignalOrdering(); // Non-blocking completion poll while the GPU is unfinished.
        CHECK(state.callbacks == 0);
    }
    state.Complete();
    if constexpr (Async) {
        std::unique_lock lock{state.mutex};
        CHECK(state.cv.wait_for(lock, std::chrono::seconds{2}, [&] { return state.callbacks == 1; }));
    } else {
        manager.WaitPendingFences(true);
        CHECK(state.callbacks == 1);
    }
    CHECK(f.words[0] == 0x12345678);
    CHECK(f.query->Registered(Fixture::Device));
    CHECK_FALSE(state.timed_out);
}

} // namespace

TEST_CASE("Payload-only report fences wait for host completion", "[video_core][report_fence]") {
    SECTION("asynchronous backend") { CheckCompletion<true>(); }
    SECTION("synchronous backend") { CheckCompletion<false>(); }
}

TEST_CASE("Pending payload reports retain their values and slots", "[video_core][report_fence]") {
    Fixture f;
    std::deque<std::function<void()>> reports, cleanup;
    f.rasterizer.fence = [&](std::function<void()>&& op) { reports.push_back(std::move(op)); };
    f.rasterizer.sync = [&](std::function<void()>&& op) { cleanup.push_back(std::move(op)); };
    f.Report(0, 0x11111111);
    f.Report(16, 0x22222222);
    CHECK(f.runtime.streamer.freed == 0);
    CHECK(f.query->Registered(Fixture::Device));
    CHECK(f.words[0] == 0);
    CHECK(f.words[4] == 0);
    REQUIRE(reports.size() == 2);
    reports[0]();
    CHECK(f.words[0] == 0x11111111);
    reports[1]();
    CHECK(f.words[4] == 0x22222222);
    // Commit queues the real cache's cleanup operation; it must retire each completed slot once.
    f.query->CommitAsyncFlushes();
    for (auto& op : cleanup) op();
    CHECK(f.runtime.streamer.freed == 2);
    CHECK_FALSE(f.query->Registered(Fixture::Device));
    CHECK_FALSE(f.query->Registered(Fixture::Device + 16));
}

TEST_CASE("Older report completion cannot unregister its replacement", "[video_core][report_fence]") {
    Fixture f;
    std::deque<std::function<void()>> reports, cleanup;
    f.rasterizer.fence = [&](std::function<void()>&& op) { reports.push_back(std::move(op)); };
    f.rasterizer.sync = [&](std::function<void()>&& op) { cleanup.push_back(std::move(op)); };
    f.Report(0, 1);
    f.Report(0, 2);
    reports[0]();
    f.query->CommitAsyncFlushes();
    cleanup.front()();
    cleanup.pop_front();
    CHECK(f.query->Registered(Fixture::Device));
    CHECK(f.runtime.streamer.freed == 1);
    reports[1]();
    CHECK(f.words[0] == 2);
    f.query->CommitAsyncFlushes();
    cleanup.front()();
    CHECK_FALSE(f.query->Registered(Fixture::Device));
    CHECK(f.runtime.streamer.freed == 2);
}

TEST_CASE("Report ordering preserves ordinary Low-accuracy fast paths", "[video_core][report_fence]") {
    Fixture f;
    EmptyCache texture, buffer;
    Completion state;
    ReportFenceManager<false> manager{f.rasterizer, *f.gpu, texture, buffer, *f.query,
                                      f.runtime, state};
    f.rasterizer.sync = [&](std::function<void()>&& op) { manager.SyncOperation(std::move(op)); };
    f.rasterizer.fence = [&](std::function<void()>&& op) { manager.SignalFence(std::move(op)); };
    u32 callbacks{};
    manager.SignalFence([&] { ++callbacks; });
    CHECK(callbacks == 1);
    CHECK(manager.created == 0);
    f.Report(0, 3, false);
    CHECK(f.words[0] == 3);
    CHECK(manager.created == 0);
    // A non-report real fence with no cache wait retains the preexisting release policy.
    texture.flush = true;
    Settings::values.gpu_fence_behavior.SetValue(Settings::GpuFenceBehavior::Balanced);
    manager.SignalFence([&] { ++callbacks; });
    CHECK_FALSE(manager.last_stubbed);
    CHECK(callbacks == 1);
    manager.SignalOrdering();
    CHECK(callbacks == 2);
    CHECK_FALSE(state.waiting);
}

TEST_CASE("Invalidated pending reports are retired once without publishing", "[video_core][report_fence]") {
    Fixture f;
    std::function<void()> report, cleanup;
    f.rasterizer.fence = [&](std::function<void()>&& op) { report = std::move(op); };
    f.rasterizer.sync = [&](std::function<void()>&& op) { cleanup = std::move(op); };
    f.Report(0, 0x12345678);
    f.query->InvalidateRegion(Fixture::Device, 4);
    report();
    CHECK(f.words[0] == 0);
    CHECK(f.runtime.streamer.freed == 0);
    f.query->CommitAsyncFlushes();
    cleanup();
    CHECK(f.runtime.streamer.freed == 1);
    CHECK_FALSE(f.query->Registered(Fixture::Device));
    f.query->CommitAsyncFlushes();
    cleanup();
    CHECK(f.runtime.streamer.freed == 1);
}

TEST_CASE("Report fence scope restores nesting and exceptional exits", "[video_core][report_fence]") {
    Fixture f;
    const bool initial = VideoCore::g_signaling_query_report_fence;
    SCOPE_EXIT { VideoCore::g_signaling_query_report_fence = initial; };
    f.rasterizer.fence = [&](std::function<void()>&&) {
        CHECK(VideoCore::g_signaling_query_report_fence);
    };
    VideoCore::g_signaling_query_report_fence = true;
    f.Report(0, 1);
    CHECK(VideoCore::g_signaling_query_report_fence);
    VideoCore::g_signaling_query_report_fence = false;
    f.Report(16, 2);
    CHECK_FALSE(VideoCore::g_signaling_query_report_fence);
    f.rasterizer.fence = [&](std::function<void()>&&) {
        CHECK(VideoCore::g_signaling_query_report_fence);
        throw std::runtime_error{"test fence failure"};
    };
    CHECK_THROWS_AS(f.Report(32, 3), std::runtime_error);
    CHECK_FALSE(VideoCore::g_signaling_query_report_fence);
}
