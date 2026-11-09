// SPDX-FileCopyrightText: 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/core_timing.h"

namespace {
// Numbers are chosen randomly to make sure the correct one is given.
constexpr std::array<u64, 5> calls_order{{2, 0, 1, 4, 3}};
std::array<s64, 5> delays{};
std::bitset<5> callbacks_ran_flags;
u64 expected_callback = 0;

template <unsigned int IDX>
std::optional<std::chrono::nanoseconds> HostCallbackTemplate(s64 time,
                                                             std::chrono::nanoseconds ns_late) {
    static_assert(IDX < callbacks_ran_flags.size(), "IDX out of range");
    callbacks_ran_flags.set(IDX);
    delays[IDX] = ns_late.count();
    ++expected_callback;
    return std::nullopt;
}

struct ScopeInit final {
    ScopeInit() {
        callbacks_ran_flags.reset();
        delays.fill(0);
        expected_callback = 0;
        core_timing.SetMulticore(true);
        core_timing.Initialize([]() {});
    }

    Core::Timing::CoreTiming core_timing;
};

u64 TestTimerSpeed(Core::Timing::CoreTiming& core_timing) {
    const u64 start = core_timing.GetGlobalTimeNs().count();
    volatile u64 placebo = 0;
    for (std::size_t i = 0; i < 1000; i++) {
        placebo = placebo + core_timing.GetGlobalTimeNs().count();
    }
    const u64 end = core_timing.GetGlobalTimeNs().count();
    return end - start;
}

} // Anonymous namespace

TEST_CASE("CoreTiming[BasicOrder]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    expected_callback = 0;
    callbacks_ran_flags.reset();

    core_timing.SyncPause(true);

    const u64 one_micro = 1000U;
    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};

        core_timing.ScheduleEvent(future_ns, events[order]);
    }
    /// test pause
    REQUIRE(callbacks_ran_flags.none());

    core_timing.Pause(false); // No need to sync

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }
}

TEST_CASE("CoreTiming[BasicOrderNoPausing]", "[core]") {
    ScopeInit guard;
    auto& core_timing = guard.core_timing;
    std::vector<std::shared_ptr<Core::Timing::EventType>> events{
        Core::Timing::CreateEvent("callbackA", HostCallbackTemplate<0>),
        Core::Timing::CreateEvent("callbackB", HostCallbackTemplate<1>),
        Core::Timing::CreateEvent("callbackC", HostCallbackTemplate<2>),
        Core::Timing::CreateEvent("callbackD", HostCallbackTemplate<3>),
        Core::Timing::CreateEvent("callbackE", HostCallbackTemplate<4>),
    };

    core_timing.SyncPause(true);
    core_timing.SyncPause(false);

    expected_callback = 0;
    callbacks_ran_flags.reset();

    const u64 start = core_timing.GetGlobalTimeNs().count();
    const u64 one_micro = 1000U;

    for (std::size_t i = 0; i < events.size(); i++) {
        const u64 order = calls_order[i];
        const auto future_ns = std::chrono::nanoseconds{static_cast<s64>(i * one_micro + 100)};
        core_timing.ScheduleEvent(future_ns, events[order]);
    }

    const u64 end = core_timing.GetGlobalTimeNs().count();
    const double scheduling_time = static_cast<double>(end - start);
    const double timer_time = static_cast<double>(TestTimerSpeed(core_timing));

    while (core_timing.HasPendingEvents())
        ;

    REQUIRE(callbacks_ran_flags.all());

    for (std::size_t i = 0; i < delays.size(); i++) {
        const double delay = static_cast<double>(delays[i]);
        const double micro = delay / 1000.0f;
        const double mili = micro / 1000.0f;
        printf("HostTimer No Pausing Delay[%zu]: %.3f %.6f\n", i, micro, mili);
    }

    const double micro = scheduling_time / 1000.0f;
    const double mili = micro / 1000.0f;
    printf("HostTimer No Pausing Scheduling Time: %.3f %.6f\n", micro, mili);
    printf("HostTimer No Pausing Timer Time: %.3f %.6f\n", timer_time / 1000.f,
           timer_time / 1000000.f);
}

TEST_CASE("CoreTiming looping deadlines exclude paused time", "[core][timing_pause]") {
    using namespace std::chrono_literals;
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    std::vector<std::pair<s64, s64>> calls;
    auto event = Core::Timing::CreateEvent(
        "skip missed ticks", [&](s64 scheduled, std::chrono::nanoseconds late) {
            calls.emplace_back(scheduled, late.count());
            const auto missed = std::max(late, 0ns) / 5ms;
            return std::optional<std::chrono::nanoseconds>{5ms * (missed + 1)};
        });
    timing.ScheduleLoopingEvent(5ms, 5ms, event);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq * 100;
    timing.Pause(false);
    const auto resumed = timing.GetGlobalTimeNs().count();
    s64 delay_ms{};
    SECTION("first callback at resume") {}
    SECTION("first callback twelve milliseconds after resume") { delay_ms = 12; }
    timing.cpu_ticks += Common::WallClock::CPUTickFreq / 1000 * delay_ms;
    const auto now = timing.GetGlobalTimeNs().count();
    const auto next = timing.Advance();
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].first == resumed);
    REQUIRE(calls[0].second == now - resumed);
    const auto next_ms = (delay_ms / 5 + 1) * 5;
    REQUIRE(next == resumed + next_ms * 1'000'000);

    timing.cpu_ticks = Common::WallClock::CPUTickFreq * 100 +
                       Common::WallClock::CPUTickFreq / 1000 * next_ms;
    REQUIRE(timing.Advance() == next.value() + 5'000'000);
    REQUIRE(calls.size() == 2);
    REQUIRE(calls[1].first == next.value());
    REQUIRE(calls[1].second == 0);
}

TEST_CASE("CoreTiming looping deadlines still skip ordinary late ticks", "[core][timing_pause]") {
    using namespace std::chrono_literals;
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    std::vector<std::pair<s64, s64>> calls;
    auto event = Core::Timing::CreateEvent(
        "ordinary late tick", [&](s64 scheduled, std::chrono::nanoseconds late) {
            calls.emplace_back(scheduled, late.count());
            return std::optional<std::chrono::nanoseconds>{5ms * (late / 5ms + 1)};
        });
    timing.Pause(false);
    timing.ScheduleLoopingEvent(5ms, 5ms, event);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq / 1000 * 17;
    REQUIRE(timing.Advance() == 20'000'000);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].first == 5'000'000);
    REQUIRE(calls[0].second == 12'000'000);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq / 1000 * 20;
    REQUIRE(timing.Advance() == 25'000'000);
    REQUIRE(calls.size() == 2);
    REQUIRE(calls[1].first == 20'000'000);
    REQUIRE(calls[1].second == 0);
}

TEST_CASE("CoreTiming fixed-period loops resume without paused lateness", "[core][timing_pause]") {
    using namespace std::chrono_literals;
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    std::vector<std::pair<s64, s64>> calls;
    auto event = Core::Timing::CreateEvent(
        "fixed period", [&](s64 scheduled, std::chrono::nanoseconds late)
                            -> std::optional<std::chrono::nanoseconds> {
            calls.emplace_back(scheduled, late.count());
            return std::nullopt;
        });
    timing.ScheduleLoopingEvent(5ms, 5ms, event);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq * 100;
    timing.Pause(false);
    const auto resumed = timing.GetGlobalTimeNs().count();
    REQUIRE(timing.Advance() == resumed + 5'000'000);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].first == resumed);
    REQUIRE(calls[0].second == 0);
    timing.cpu_ticks += Common::WallClock::CPUTickFreq / 200;
    REQUIRE(timing.Advance() == resumed + 10'000'000);
    REQUIRE(calls.size() == 2);
    REQUIRE(calls[1].first == resumed + 5'000'000);
    REQUIRE(calls[1].second == 0);
}

TEST_CASE("CoreTiming one-shot deadlines retain paused lateness", "[core][timing_pause]") {
    using namespace std::chrono_literals;
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    std::vector<std::pair<s64, s64>> calls;
    auto event = Core::Timing::CreateEvent(
        "one-shot", [&](s64 scheduled, std::chrono::nanoseconds late)
                        -> std::optional<std::chrono::nanoseconds> {
            calls.emplace_back(scheduled, late.count());
            return std::nullopt;
        });
    timing.ScheduleEvent(5ms, event);
    timing.cpu_ticks = Common::WallClock::CPUTickFreq * 100;
    timing.Pause(false);
    REQUIRE_FALSE(timing.Advance().has_value());
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].first == 5'000'000);
    REQUIRE(calls[0].second == 99'995'000'000);
}
