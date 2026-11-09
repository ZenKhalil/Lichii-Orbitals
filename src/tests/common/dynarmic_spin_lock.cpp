// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/cpu_features.h"
#include "dynarmic/common/spin_lock_x64.h"

namespace {
using namespace Xbyak::util;

constexpr std::array<std::uint64_t, 3> Sentinels{
    0x1122334455667788ULL, 0x8877665544332211ULL, 0xaabbccdd12345678ULL};

std::size_t UniqueInstruction(std::span<const std::uint8_t> code,
                              std::span<const std::uint8_t> instruction) {
    const auto match = std::search(code.begin(), code.end(), instruction.begin(), instruction.end());
    REQUIRE(match != code.end());
    REQUIRE(std::search(match + instruction.size(), code.end(), instruction.begin(),
                        instruction.end()) == code.end());
    return static_cast<std::size_t>(match - code.begin());
}

void ReplaceInstruction(Xbyak::CodeGenerator& code, std::size_t offset, std::size_t size,
                        std::span<const std::uint8_t> replacement) {
    REQUIRE(replacement.size() <= size);
    auto* destination = const_cast<std::uint8_t*>(code.getCode()) + offset;
    std::memset(destination, 0x90, size);
    std::memcpy(destination, replacement.data(), replacement.size());
}
} // namespace

TEST_CASE("Dynarmic generated lock rechecks after arming the monitor",
          "[common][dynarmic_spin_lock][dynarmic_spin_lock_race]") {
    enum class Interleaving { ReleaseBeforeArm, HeldUntilWait, AlreadyFree };
    for (const auto pointer : {rcx, rax, rbx, rdx, r10}) {
        for (const auto scenario : {Interleaving::ReleaseBeforeArm, Interleaving::HeldUntilWait,
                                    Interleaving::AlreadyFree}) {
            CAPTURE(pointer.getIdx(), static_cast<int>(scenario));
            Xbyak::CodeGenerator code;
            code.push(rbx);
            code.mov(pointer, rcx);
            if (pointer != rax) code.mov(rax, Sentinels[0]);
            if (pointer != rbx) code.mov(rbx, Sentinels[1]);
            if (pointer != rdx) code.mov(rdx, Sentinels[2]);
            const auto lock_start = code.getSize();
            Dynarmic::EmitSpinLockLock(code, pointer, r11d, true);
            const auto lock_size = code.getSize() - lock_start;
            code.mov(code.qword[r9], rax);
            code.mov(code.qword[r9 + 8], rbx);
            code.mov(code.qword[r9 + 16], rdx);
            code.mov(code.qword[r9 + 24], pointer);
            code.pop(rbx);
            code.ret();

            std::vector<std::uint8_t> monitor{0xf3};
            if (pointer.getIdx() >= 8) monitor.push_back(0x41);
            monitor.insert(monitor.end(), {0x0f, 0xae,
                static_cast<std::uint8_t>(0xf0 | (pointer.getIdx() & 7))});
            constexpr std::array<std::uint8_t, 4> wait{0xf2, 0x0f, 0xae, 0xf3};
            const std::span<const std::uint8_t> emitted{code.getCode() + lock_start, lock_size};
            const auto monitor_offset = lock_start + UniqueInstruction(emitted, monitor);
            const auto wait_offset = lock_start + UniqueInstruction(emitted, wait);
            REQUIRE(monitor_offset < wait_offset);

            // Replace only the two hardware instructions, retaining every emitted branch.
            // The first stub models the owner releasing between failed XCHG and UMONITOR.
            Xbyak::CodeGenerator release;
            release.and_(release.dword[pointer], 0);
            if (scenario == Interleaving::ReleaseBeforeArm) {
                ReplaceInstruction(code, monitor_offset, monitor.size(),
                                   {release.getCode(), release.getSize()});
            } else {
                ReplaceInstruction(code, monitor_offset, monitor.size(), {});
            }
            Xbyak::CodeGenerator wait_stub;
            if (scenario == Interleaving::HeldUntilWait) {
                // RCX retains the first argument even when the emitter uses RAX/RBX/RDX.
                wait_stub.and_(wait_stub.dword[rcx], 0);
            } else {
                wait_stub.inc(wait_stub.dword[r8]);
            }
            ReplaceInstruction(code, wait_offset, wait.size(),
                               {wait_stub.getCode(), wait_stub.getSize()});
            code.ready();

            std::uint32_t lock = scenario == Interleaving::AlreadyFree ? 0 : 1;
            std::uint32_t waits = 0;
            std::array<std::uint64_t, 4> saved{};
            using Function = void (*)(std::uint32_t*, std::uint64_t, std::uint32_t*,
                                       std::uint64_t*);
            code.getCode<Function>()(&lock, 0, &waits, saved.data());
            CHECK(waits == 0);
            CHECK(lock == 1); // Every successful path must still acquire through XCHG.
            const auto address = reinterpret_cast<std::uint64_t>(&lock);
            CHECK(saved[0] == (pointer == rax ? address : Sentinels[0]));
            CHECK(saved[1] == (pointer == rbx ? address : Sentinels[1]));
            CHECK(saved[2] == (pointer == rdx ? address : Sentinels[2]));
            CHECK(saved[3] == address);
        }
    }
}

TEST_CASE("Dynarmic generated lock serializes real contending threads",
          "[common][dynarmic_spin_lock][dynarmic_spin_lock_contention]") {
    for (const bool waitpkg : {false, true}) {
        if (waitpkg && !Common::g_cpu_caps.waitpkg) continue;
        CAPTURE(waitpkg);
        Xbyak::CodeGenerator code;
        const auto lock_offset = code.getSize();
        Dynarmic::EmitSpinLockLock(code, rcx, eax, waitpkg);
        code.ret();
        const auto unlock_offset = code.getSize();
        Dynarmic::EmitSpinLockUnlock(code, rcx, eax);
        code.ret();
        code.ready();
        using Function = void (*)(std::uint32_t*);
        const auto lock_function = reinterpret_cast<Function>(code.getCode<std::uint8_t*>() + lock_offset);
        const auto unlock_function = reinterpret_cast<Function>(code.getCode<std::uint8_t*>() + unlock_offset);
        std::uint32_t lock = 0;
        std::atomic<unsigned> inside{}, counter{};
        std::atomic<bool> overlap{};
        constexpr unsigned Threads = 4, Iterations = 1000;
        std::barrier start{Threads};
        std::vector<std::thread> workers;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned thread = 0; thread < Threads; ++thread) {
            workers.emplace_back([&] {
                start.arrive_and_wait();
                for (unsigned i = 0; i < Iterations; ++i) {
                    lock_function(&lock);
                    if (inside.fetch_add(1) != 0) overlap = true;
                    const auto value = counter.load(std::memory_order_relaxed);
                    if (i % 64 == 0) std::this_thread::yield();
                    counter.store(value + 1, std::memory_order_relaxed);
                    inside.fetch_sub(1);
                    unlock_function(&lock);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        const auto milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        std::printf("Dynarmic contention WAITPKG=%d: %u acquisitions in %.3f ms (no timing threshold)\n",
                    waitpkg, Threads * Iterations, milliseconds);
        CHECK_FALSE(overlap.load());
        CHECK(counter.load() == Threads * Iterations);
        CHECK(inside.load() == 0);
        CHECK(lock == 0);
    }
}

TEST_CASE("Dynarmic generated lock polls without atomic writes after spurious wakes",
          "[common][dynarmic_spin_lock][dynarmic_spin_lock_spurious]") {
    Xbyak::CodeGenerator code;
    Dynarmic::EmitSpinLockLock(code, rcx, r11d, true);
    code.ret();
    const std::span<const std::uint8_t> emitted{code.getCode(), code.getSize()};
    constexpr std::array<std::uint8_t, 4> monitor{0xf3, 0x0f, 0xae, 0xf1};
    constexpr std::array<std::uint8_t, 4> wait{0xf2, 0x0f, 0xae, 0xf3};
    constexpr std::array<std::uint8_t, 3> exchange{0x44, 0x87, 0x19};
    const auto monitor_offset = UniqueInstruction(emitted, monitor);
    const auto wait_offset = UniqueInstruction(emitted, wait);
    const auto exchange_offset = UniqueInstruction(emitted, exchange);

    // Simulate two spurious wakes while the owner still holds the lock, then release.
    const auto wait_stub = code.getSize();
    code.inc(code.dword[r8]);
    code.cmp(code.dword[r8], 3);
    Xbyak::Label return_from_wait;
    code.jne(return_from_wait, code.T_NEAR);
    code.and_(code.dword[rcx], 0);
    code.L(return_from_wait);
    code.jmp(code.getCode() + wait_offset + wait.size(), code.T_NEAR);

    // Count atomic attempts while still executing the emitter's actual XCHG operation.
    const auto exchange_stub = code.getSize();
    code.inc(code.dword[r9]);
    code.xchg(code.dword[rcx], r11d);
    code.jmp(code.getCode() + exchange_offset + exchange.size(), code.T_NEAR);
    const auto jump_to = [&](std::size_t offset, std::size_t width, std::size_t target) {
        const auto distance = static_cast<std::ptrdiff_t>(target) -
                              static_cast<std::ptrdiff_t>(offset + 2);
        REQUIRE(distance >= -128);
        REQUIRE(distance <= 127);
        const std::array<std::uint8_t, 2> jump{
            0xeb, static_cast<std::uint8_t>(static_cast<std::int8_t>(distance))};
        ReplaceInstruction(code, offset, width, jump);
    };
    ReplaceInstruction(code, monitor_offset, monitor.size(), {});
    jump_to(wait_offset, wait.size(), wait_stub);
    jump_to(exchange_offset, exchange.size(), exchange_stub);
    code.ready();

    std::uint32_t lock = 1, waits = 0, attempts = 0;
    using Function = void (*)(std::uint32_t*, std::uint64_t, std::uint32_t*, std::uint32_t*);
    code.getCode<Function>()(&lock, 0, &waits, &attempts);
    CHECK(waits == 3);
    CHECK(attempts == 2); // Initial failed acquisition and the successful acquisition only.
    CHECK(lock == 1);
}
