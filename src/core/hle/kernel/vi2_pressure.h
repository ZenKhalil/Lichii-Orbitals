// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace Kernel::Vi2Pressure {
using Word = std::uint64_t;
static_assert(std::atomic<Word>::is_always_lock_free);
inline bool ParseEnabled(const char* value) {
    return value && value[0] == '1' && value[1] == '\0';
}
// Startup-only: do not initialize getenv's CRT machinery under the scheduler lock.
inline const bool enabled = ParseEnabled(std::getenv("EDEN_NX2_VI2_PRESSURE"));
inline Word Now() noexcept {
    return static_cast<Word>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
constexpr Word StaleNs = 100'000'000;
constexpr Word PublishPeriodNs = 1'000'000;
constexpr Word RowLimit = 128;
enum class Kind { Application, Hle, Other };
struct Snapshot {
    Word generation{}, stamp{}, kernel_tag{}, current_id{}, application{}, hle{}, other{}, scanned{};
    Word min_priority{64}, current_priority{64};
    bool current_found{}, current_hle{}, capped{}, pinned{};
};

// Called only under the existing scheduler lock. Actual scheduled queue, not affinity hints.
// The sampled current pointer is dereferenced only after finding the queue-owned node.
template <typename Queue, typename Thread, typename Classify, typename Pinned>
Snapshot CountQueue(const Queue& queue, const Thread* current, Classify classify, Pinned pinned) {
    Snapshot result;
    auto* thread = queue.GetScheduledFront(3);
    for (; thread && result.scanned < RowLimit;
         thread = queue.GetScheduledNext(3, thread)) {
        ++result.scanned;
        const Kind kind = classify(*thread);
        if (thread == current) {
            result.current_found = true;
            result.current_id = thread->GetThreadId();
            result.current_priority = thread->GetPriority();
            result.current_hle = kind == Kind::Hle;
            if (result.current_hle) continue;
        }
        if (kind == Kind::Application) {
            ++result.application;
            result.min_priority = std::min(result.min_priority,
                                          static_cast<Word>(thread->GetPriority()));
            result.pinned |= pinned(*thread);
        } else if (kind == Kind::Hle) {
            ++result.hle;
        } else {
            ++result.other;
        }
    }
    result.capped = thread != nullptr;
    return result;
}

// All publication operations are SC, making the bounded sequence check straightforward.
// No non-atomic payload, allocation, mutex, logging, or unbounded reader retry.
struct Publication {
    std::atomic<Word> sequence{}, stamp{}, kernel_tag{}, current_id{}, counts{}, priorities{}, flags{};
    std::atomic<Word> next_publish{}, update_epoch{}, published_epoch{};
    // Bounded global try-claim serializes even distinct KernelSystems. Gate before queue walking.
    template <typename MakeSnapshot>
    bool TryPublish(Word now, MakeSnapshot make_snapshot) noexcept {
        // Even a throttled queue update invalidates the old view; never call stale state zero work.
        const Word epoch = update_epoch.fetch_add(1) + 1;
        if (now < next_publish.load()) return false;
        Word before = sequence.load();
        if ((before & 1) || !sequence.compare_exchange_strong(before, before + 1)) return false;
        if (now < next_publish.load()) { sequence.store(before); return false; }
        Snapshot s;
        try { s = make_snapshot(); }
        catch (...) { sequence.store(before); return false; }
        stamp.store(now);
        kernel_tag.store(s.kernel_tag);
        current_id.store(s.current_id);
        counts.store(s.application | (s.hle << 8) | (s.other << 16) | (s.scanned << 24));
        priorities.store(s.min_priority | (s.current_priority << 8));
        flags.store(Word{s.current_found} | (Word{s.current_hle} << 1) |
                    (Word{s.capped} << 2) | (Word{s.pinned} << 3));
        published_epoch.store(epoch);
        next_publish.store(now + PublishPeriodNs);
        sequence.store(before + 2);
        return true;
    }
    std::optional<Snapshot> Read() const noexcept {
        for (unsigned attempt = 0; attempt != 2; ++attempt) {
            const Word epoch = update_epoch.load();
            const Word first = sequence.load();
            if (!first || (first & 1)) continue;
            Snapshot s;
            s.generation = first;
            s.stamp = stamp.load();
            s.kernel_tag = kernel_tag.load();
            s.current_id = current_id.load();
            const Word c = counts.load(), p = priorities.load(), f = flags.load();
            if (sequence.load() != first || published_epoch.load() != epoch ||
                update_epoch.load() != epoch) continue;
            s.application = c & 255; s.hle = (c >> 8) & 255;
            s.other = (c >> 16) & 255; s.scanned = (c >> 24) & 255;
            s.min_priority = p & 255; s.current_priority = (p >> 8) & 255;
            s.current_found = f & 1; s.current_hle = f & 2;
            s.capped = f & 4; s.pinned = f & 8;
            return s;
        }
        return std::nullopt;
    }
};
inline Publication publication;

struct Observations {
    Word samples{}, positive{}, zero{}, pre_wait{}, stale{}, unknown{}, capped{}, mismatch{}, pinned{};
    Word max_age{}, max_application{}, max_hle{}, min_priority{64}, last_generation{};
    void Add(const std::optional<Snapshot>& s, Word kernel, Word self, Word begin, Word now) noexcept {
        ++samples;
        if (!s || now < s->stamp) { ++unknown; return; }
        const Word age = now - s->stamp;
        max_age = std::max(max_age, age);
        last_generation = s->generation;
        if (s->kernel_tag != kernel || !s->current_found || !s->current_hle || s->current_id != self) {
            ++mismatch; ++unknown; return;
        }
        if (s->capped) { ++capped; ++unknown; return; }
        if (age > StaleNs) { ++stale; return; }
        if (s->stamp < begin) { ++pre_wait; return; }
        // These are assigned runnable candidates, not proof of dispatch eligibility or lost FPS.
        ++(s->application ? positive : zero);
        pinned += s->pinned;
        max_application = std::max(max_application, s->application);
        max_hle = std::max(max_hle, s->hle);
        min_priority = std::min(min_priority, s->min_priority);
    }
};

struct Totals {
    std::atomic<Word> waits{}, wait_ns{}, other_waits{}, other_ns{}, samples{}, positive{}, zero{};
    std::atomic<Word> pre_wait{}, stale{}, unknown{}, capped{}, mismatch{}, pinned{}, next_report{};
};
inline Totals totals;
struct Report {
    Word waits{}, wait_ns{}, other_waits{}, other_ns{}, samples{}, positive{}, zero{};
    Word pre_wait{}, stale{}, unknown{}, capped{}, mismatch{}, pinned{}, last_ns{};
};
class WaitScope {
public:
    WaitScope(bool active_, Word kernel_, Word self_, bool core3_) noexcept
        : active{active_}, core3{core3_}, kernel{kernel_}, self{self_} {
        if (active) { begin = Now(); Observe(); }
    }
    void Observe() noexcept {
        if (active && core3) observations.Add(publication.Read(), kernel, self, begin, Now());
    }
    std::optional<Report> Finish() noexcept {
        if (!active) return std::nullopt;
        Observe();
        const Word end = Now(), duration = end - begin;
        if (core3) {
            totals.waits.fetch_add(1); totals.wait_ns.fetch_add(duration);
            totals.samples.fetch_add(observations.samples);
            totals.positive.fetch_add(observations.positive); totals.zero.fetch_add(observations.zero);
            totals.pre_wait.fetch_add(observations.pre_wait);
            totals.stale.fetch_add(observations.stale); totals.unknown.fetch_add(observations.unknown);
            totals.capped.fetch_add(observations.capped); totals.mismatch.fetch_add(observations.mismatch);
            totals.pinned.fetch_add(observations.pinned);
        } else {
            totals.other_waits.fetch_add(1); totals.other_ns.fetch_add(duration);
        }
        Word next = totals.next_report.load();
        if (end < next || !totals.next_report.compare_exchange_strong(next, end + 5'000'000'000))
            return std::nullopt;
        // Concurrent cumulative fields may be sampled between another scope's increments.
        return Report{totals.waits.load(), totals.wait_ns.load(), totals.other_waits.load(),
            totals.other_ns.load(), totals.samples.load(), totals.positive.load(), totals.zero.load(),
            totals.pre_wait.load(), totals.stale.load(), totals.unknown.load(), totals.capped.load(), totals.mismatch.load(),
            totals.pinned.load(), duration};
    }
    Observations observations;
private:
    bool active{}, core3{};
    Word kernel{}, self{}, begin{};
};
} // namespace Kernel::Vi2Pressure
