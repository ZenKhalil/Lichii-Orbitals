// SPDX-FileCopyrightText: Copyright 2026 Lichii Orbitals Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Default-off observer. Never changes dirty flags or emitted commands.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace Vulkan::ViewportDependencyTrace {

inline bool Enabled() {
    static const bool enabled = [] {
        const char* p = std::getenv("EDEN_NX2_VIEWPORT_DEPENDENCY_TRACE");
        return p && std::string_view{p} == "1";
    }();
    return enabled;
}

// This protects diagnostics only. Never wait/spin and never change renderer state.
struct Owner {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> identity{}, skipped{}, mismatches{};
    std::atomic<bool> unknown{}, warned{};
    struct Access {
        Owner* owner{};
        Access() = default;
        explicit Access(Owner* value) : owner{value} {}
        Access(const Access&) = delete;
        Access& operator=(const Access&) = delete;
        ~Access() {
            if (owner) {
                owner->busy.clear(std::memory_order_release);
            }
        }
        explicit operator bool() const {
            return owner != nullptr;
        }
    };

    Access TryAccess() {
        if (busy.test_and_set(std::memory_order_acquire)) {
            skipped.fetch_add(1, std::memory_order_relaxed);
            unknown.store(true, std::memory_order_release);
            return {};
        }
        static std::atomic<std::uint64_t> next_identity{1};
        static thread_local const auto current =
            next_identity.fetch_add(1, std::memory_order_relaxed);
        auto original = identity.load(std::memory_order_relaxed);
        if (!original) {
            identity.store(current, std::memory_order_relaxed);
        } else if (original != current) {
            mismatches.fetch_add(1, std::memory_order_relaxed);
            unknown.store(true, std::memory_order_release);
        }
        if (unknown.load(std::memory_order_acquire)) {
            busy.clear(std::memory_order_release);
            return {};
        }
        return Access{this};
    }

    bool WarnOnce() {
        return unknown.load(std::memory_order_acquire) && !warned.exchange(true);
    }
};

struct Depth {
    std::uint32_t minimum{}, maximum{};
    bool operator==(const Depth&) const = default;
};

struct Record {
    std::uint64_t frame{};
    std::int32_t channel{-1};
    std::uint32_t old_mode{}, mode{}, mask{}, index{};
    Depth old_depth{}, needed_depth{};
};

struct State {
    std::int32_t channel{-1};
    bool seen{}, cached{}, cached_enabled{};
    std::uint32_t seen_mode{}, cached_mode{}, cached_count{};
    std::array<Depth, 16> depths{};
    std::uint64_t observations{}, mode_changes{}, clean_mode_changes{}, stale_draws{},
        depth_mismatches{};
    std::uint64_t last_report_ns{}, frame{};
    std::array<Record, 32> records{};
    std::size_t stored{}, printed{};
    Record last{};
    bool has_last{};

    void Channel(std::int32_t value) {
        channel = value;
        seen = false;
    }

    template <class Compute>
    void Observe(std::uint32_t mode, bool enabled, bool dirty, std::uint64_t frame,
                 Compute&& compute, std::uint32_t count = 16) {
        ++observations;
        if (seen && seen_mode != mode) {
            ++mode_changes;
            if (!dirty) {
                ++clean_mode_changes;
            }
        }
        seen = true;
        seen_mode = mode;
        if (dirty || !enabled || !cached || !cached_enabled || cached_mode == mode) {
            return;
        }
        ++stale_draws;
        Record r{frame, channel, cached_mode, mode};
        for (std::uint32_t i = 0; i < std::min({count, cached_count, 16u}); ++i) {
            const Depth wanted = compute(i);
            if (wanted == depths[i]) {
                continue;
            }
            if (!r.mask) {
                r.index = i;
                r.old_depth = depths[i];
                r.needed_depth = wanted;
            }
            r.mask |= 1u << i;
        }
        if (!r.mask) {
            return;
        }
        ++depth_mismatches;
        // Keep first 32 distinct frame/channel/mode records, and always retain latest.
        const bool distinct =
            !has_last || last.frame != frame || last.channel != channel || last.mode != mode;
        last = r;
        has_last = true;
        if (distinct && stored < records.size()) {
            records[stored++] = r;
        }
    }

    template <class Compute>
    void Emitted(std::uint32_t mode, bool enabled, Compute&& compute, std::uint32_t count = 16) {
        cached = true;
        cached_mode = mode;
        cached_enabled = enabled;
        cached_count = std::min(count, 16u);
        if (enabled) {
            for (std::uint32_t i = 0; i < cached_count; ++i) {
                depths[i] = compute(i);
            }
        }
    }

    bool ReportDue(std::uint64_t now) {
        if (!last_report_ns) {
            last_report_ns = now;
            return false;
        }
        if (now < last_report_ns || now - last_report_ns < 5'000'000'000ULL) {
            return false;
        }
        last_report_ns = now;
        return true;
    }
};

} // namespace Vulkan::ViewportDependencyTrace
