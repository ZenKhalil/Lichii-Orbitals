// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace Common::CpuReadTiming {
using Count = std::uint64_t;
static_assert(std::atomic<Count>::is_always_lock_free);

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_CPU_READ_TIMING");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

struct SteadyClock {
    static Count Now() noexcept {
        return static_cast<Count>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

struct Counters {
    std::atomic<Count> first_ns{}, started{}, completed{}, aborted{}, in_flight{};
    std::atomic<Count> reactive{}, preemptive{}, lookup_ns{}, queue_ns{}, wait_ns{}, total_ns{};
    std::atomic<Count> max_total_ns{}, max_wait_ns{}, next_report_ns{};
};
inline Counters counters;

inline void RaiseMaximum(std::atomic<Count>& maximum, Count value) noexcept {
    Count old = maximum.load(std::memory_order_relaxed);
    while (old < value && !maximum.compare_exchange_weak(old, value, std::memory_order_relaxed)) {}
}

// All OnCPURead callers, not monitor ownership. No allocation, logger, or diagnostic lock.
template <typename Clock = SteadyClock>
class ReadScope {
public:
    explicit ReadScope(bool enabled = Enabled()) noexcept : active{enabled} {
        if (!active) return;
        begin = phase_begin = Clock::Now();
        Count unset{};
        counters.first_ns.compare_exchange_strong(unset, begin, std::memory_order_relaxed);
        counters.started.fetch_add(1, std::memory_order_relaxed);
        counters.in_flight.fetch_add(1, std::memory_order_relaxed);
    }
    ReadScope(const ReadScope&) = delete;
    ReadScope& operator=(const ReadScope&) = delete;
    ~ReadScope() noexcept {
        if (!active) return;
        const Count end = Clock::Now();
        RecordPhase(end);
        const Count total = end - begin;
        counters.lookup_ns.fetch_add(lookup, std::memory_order_relaxed);
        counters.queue_ns.fetch_add(queue, std::memory_order_relaxed);
        counters.wait_ns.fetch_add(wait, std::memory_order_relaxed);
        counters.total_ns.fetch_add(total, std::memory_order_relaxed);
        RaiseMaximum(counters.max_total_ns, total);
        RaiseMaximum(counters.max_wait_ns, wait);
        (complete ? counters.completed : counters.aborted).fetch_add(1, std::memory_order_relaxed);
        counters.in_flight.fetch_sub(1, std::memory_order_relaxed);
    }
    void LookupComplete(bool preemptive) noexcept {
        if (!active) return;
        const Count stamp = Clock::Now();
        RecordPhase(stamp);
        (preemptive ? counters.preemptive : counters.reactive).fetch_add(1, std::memory_order_relaxed);
        phase = preemptive ? Phase::None : Phase::Queue;
        phase_begin = stamp;
    }
    void BeginWait() noexcept {
        if (!active) return;
        const Count stamp = Clock::Now();
        RecordPhase(stamp);
        phase = Phase::Wait;
        phase_begin = stamp;
    }
    void Complete() noexcept { complete = true; }

private:
    enum class Phase { Lookup, Queue, Wait, None };
    void RecordPhase(Count stamp) noexcept {
        switch (phase) {
        case Phase::Lookup: lookup += stamp - phase_begin; break;
        case Phase::Queue: queue += stamp - phase_begin; break;
        case Phase::Wait: wait += stamp - phase_begin; break;
        case Phase::None: break;
        }
    }
    bool active{}, complete{};
    Phase phase{Phase::Lookup};
    Count begin{}, phase_begin{}, lookup{}, queue{}, wait{};
};

struct Snapshot {
    Count elapsed_ns{}, started{}, completed{}, aborted{}, in_flight{}, reactive{}, preemptive{};
    Count lookup_ns{}, queue_ns{}, wait_ns{}, total_ns{}, max_total_ns{}, max_wait_ns{};
};

// Cumulative monotonic counters: no reset races or claim of an atomic multi-field snapshot.
inline std::optional<Snapshot> PollReportAt(Count stamp) noexcept {
    constexpr Count Interval = 10'000'000'000ULL;
    const Count first = counters.first_ns.load(std::memory_order_relaxed);
    if (first == 0 || stamp < first || stamp - first < Interval) return std::nullopt;
    Count next = counters.next_report_ns.load(std::memory_order_relaxed);
    if (stamp < next || !counters.next_report_ns.compare_exchange_strong(
            next, stamp + Interval, std::memory_order_relaxed)) return std::nullopt;
    return Snapshot{
        stamp - first,
        counters.started.load(std::memory_order_relaxed),
        counters.completed.load(std::memory_order_relaxed),
        counters.aborted.load(std::memory_order_relaxed),
        counters.in_flight.load(std::memory_order_relaxed),
        counters.reactive.load(std::memory_order_relaxed),
        counters.preemptive.load(std::memory_order_relaxed),
        counters.lookup_ns.load(std::memory_order_relaxed),
        counters.queue_ns.load(std::memory_order_relaxed),
        counters.wait_ns.load(std::memory_order_relaxed),
        counters.total_ns.load(std::memory_order_relaxed),
        counters.max_total_ns.load(std::memory_order_relaxed),
        counters.max_wait_ns.load(std::memory_order_relaxed),
    };
}

// Caller must be outside the monitor/cache critical section. Check the clock at most every
// 64 JIT returns per host thread; a stalled core need not be the one that reports.
inline std::optional<Snapshot> PollReport() noexcept {
    if (!Enabled()) return std::nullopt;
    thread_local unsigned returns{};
    if (++returns % 64 != 0) return std::nullopt;
    return PollReportAt(SteadyClock::Now());
}
} // namespace Common::CpuReadTiming
