// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/common/exclusive_wait_policy.h"
#include "dynarmic/common/spin_lock_x64.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace {
using namespace Xbyak::util;

class GeneratedPauseLock {
public:
    using Function = void (*)(std::uint32_t*);

    explicit GeneratedPauseLock(bool double_pause) {
        Dynarmic::EmitSpinLockLock(code, rcx, eax, false, double_pause);
        code.ret();
        const auto release_offset = code.getSize();
        Dynarmic::EmitSpinLockUnlock(code, rcx, eax);
        code.ret();
        code.ready();
        acquire = code.getCode<Function>();
        release = reinterpret_cast<Function>(code.getCode<std::uint8_t*>() + release_offset);
    }

    Function acquire{}, release{};

private:
    Xbyak::CodeGenerator code;
};

struct GeneratedGuard {
    GeneratedPauseLock& generated;
    std::uint32_t& word;

    GeneratedGuard(GeneratedPauseLock& generated_, std::uint32_t& word_)
        : generated{generated_}, word{word_} {
        generated.acquire(&word);
    }
    ~GeneratedGuard() {
        generated.release(&word);
    }
};

// Separate code callbacks make guest instructions independent of the data arena.
// Both JITs share this real fastmem backing and the actual ExclusiveMonitor.
// No C++ exception or Catch assertion is thrown from a monitor-held callback.
struct ExclusiveArena {
    alignas(4096) std::array<std::uint8_t, 4096> bytes{};

    void Store(std::uint64_t address, std::uint64_t value) {
        std::memcpy(bytes.data() + address, &value, sizeof(value));
    }
    std::uint64_t Load(std::uint64_t address) const {
        std::uint64_t value{};
        std::memcpy(&value, bytes.data() + address, sizeof(value));
        return value;
    }
};

class ExclusiveEnvironment final : public Dynarmic::A64::UserCallbacks {
public:
    // Encodings checked against frontend/A64/decoder/a64.inc's exact patterns.
    static constexpr std::uint64_t LoadPc = 0x10000, StorePc = 0x10010, ClearPc = 0x10020;
    static constexpr std::uint32_t Ldxr = 0xc85f7c61; // LDXR X1, [X3]
    static constexpr std::uint32_t Stxr = 0xc8047c65; // STXR W4, X5, [X3]
    static constexpr std::uint32_t Clrex = 0xd5033f5f;
    // SVC cannot delimit this test: EmitA64CallSupervisor clears the local
    // exclusive state after its callback. BRK's exception path does not.
    static constexpr std::uint32_t Stop = 0xd4200000;

    explicit ExclusiveEnvironment(ExclusiveArena& arena_) : arena{arena_} {}

    std::optional<std::uint32_t> MemoryReadCode(std::uint64_t address) override {
        if (address == LoadPc) {
            return Ldxr;
        }
        if (address == StorePc) {
            return Stxr;
        }
        if (address == ClearPc) {
            return Clrex;
        }
        if (address == LoadPc + 4 || address == StorePc + 4 || address == ClearPc + 4) {
            return Stop;
        }
        return std::nullopt;
    }
    bool Valid(std::uint64_t address, std::size_t size) {
        const bool valid = size <= arena.bytes.size() && address <= arena.bytes.size() - size;
        bad |= !valid;
        return valid;
    }
    std::uint64_t MemoryRead(std::uint64_t address, std::size_t size) override {
        ++reads;
        std::uint64_t value{};
        if (size > sizeof(value) || !Valid(address, size)) {
            bad = true;
            return 0;
        }
        std::memcpy(&value, arena.bytes.data() + address, size);
        return value;
    }
    Dynarmic::A64::Vector MemoryRead128(std::uint64_t) override {
        bad = true;
        return {};
    }
    void MemoryWrite(std::uint64_t address, std::uint64_t value, std::size_t size) override {
        ++writes;
        if (size <= sizeof(value) && Valid(address, size)) {
            std::memcpy(arena.bytes.data() + address, &value, size);
        } else {
            bad = true;
        }
    }
    void MemoryWrite128(std::uint64_t, Dynarmic::A64::Vector) override {
        bad = true;
    }
    bool MemoryWriteExclusive64(std::uint64_t address, std::uint64_t value,
                                std::uint64_t expected) override {
        ++exclusive_writes;
        if (!Valid(address, sizeof(value)) || arena.Load(address) != expected) {
            return false;
        }
        arena.Store(address, value);
        return true;
    }
    void CallSVC(std::uint32_t) override {
        bad = true;
        jit->HaltExecution();
    }
    void ExceptionRaised(std::uint64_t, Dynarmic::A64::Exception exception) override {
        bad |= exception != Dynarmic::A64::Exception::Breakpoint;
        ++stops;
        jit->HaltExecution();
    }
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override {
        return 1000;
    }
    std::uint64_t GetCNTPCT() override {
        return 0;
    }

    ExclusiveArena& arena;
    Dynarmic::A64::Jit* jit{};
    bool bad{};
    unsigned reads{}, writes{}, exclusive_writes{}, stops{};
};

Dynarmic::A64::UserConfig ExclusiveConfig(ExclusiveEnvironment& env,
                                          Dynarmic::ExclusiveMonitor& monitor,
                                          std::size_t processor, bool inline_fastmem) {
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &monitor;
    config.processor_id = processor;
    config.enable_cycle_counting = false;
    if (inline_fastmem) {
        config.fastmem_pointer = reinterpret_cast<std::uintptr_t>(env.arena.bytes.data());
        config.fastmem_address_space_bits = 12;
        config.silently_mirror_fastmem = false;
        config.fastmem_exclusive_access = true;
    }
    return config;
}

void ExecuteExclusive(Dynarmic::A64::Jit& jit, ExclusiveEnvironment& env, std::uint64_t pc,
                      std::uint64_t address, std::uint64_t value = 0) {
    const auto stops = env.stops;
    jit.ClearHalt();
    jit.SetPC(pc);
    jit.SetRegister(3, address);
    jit.SetRegister(4, ~std::uint64_t{0});
    jit.SetRegister(5, value);
    jit.Run();
    REQUIRE_FALSE(env.bad);
    REQUIRE(env.stops == stops + 1);
}
} // namespace

TEST_CASE("Dynarmic PAUSE loops retain held reads and retry acquisition after release",
          "[common][dynarmic_pause2][dynarmic_pause2_native]") {
    for (const bool double_pause : {false, true}) {
        for (const auto pointer : {rcx, rax, rbx, rdx, r10}) {
            CAPTURE(double_pause, pointer.getIdx());
            Xbyak::CodeGenerator code;
            code.push(rbx);
            code.push(r12);
            code.mov(r12, rdx); // saved-output pointer, before RDX sentinel setup
            code.mov(pointer, rcx);
            constexpr std::array<std::uint64_t, 3> Sentinels{0x1122334455667788, 0x8877665544332211,
                                                             0xaabbccdd12345678};
            if (pointer != rax) {
                code.mov(rax, Sentinels[0]);
            }
            if (pointer != rbx) {
                code.mov(rbx, Sentinels[1]);
            }
            if (pointer != rdx) {
                code.mov(rdx, Sentinels[2]);
            }
            const auto begin = code.getSize();
            Dynarmic::EmitSpinLockLock(code, pointer, r11d, false, double_pause);
            const auto end = code.getSize();
            code.mov(code.qword[r12], rax);
            code.mov(code.qword[r12 + 8], rbx);
            code.mov(code.qword[r12 + 16], rdx);
            code.mov(code.qword[r12 + 24], pointer);
            code.pop(r12);
            code.pop(rbx);
            code.ret();

            std::vector<std::size_t> pauses;
            for (auto offset = begin; offset + 2 <= end; ++offset) {
                if (code.getCode()[offset] == 0xf3 && code.getCode()[offset + 1] == 0x90) {
                    pauses.push_back(offset);
                }
            }
            REQUIRE(pauses.size() == (double_pause ? 2 : 1));
            Xbyak::CodeGenerator exchange;
            exchange.xchg(exchange.dword[pointer], r11d);
            const std::span<const std::uint8_t> body{code.getCode() + begin, end - begin};
            const auto found = std::search(body.begin(), body.end(), exchange.getCode(),
                                           exchange.getCode() + exchange.getSize());
            REQUIRE(found != body.end());
            REQUIRE(std::search(found + exchange.getSize(), body.end(), exchange.getCode(),
                                exchange.getCode() + exchange.getSize()) == body.end());
            const auto exchange_offset = begin + static_cast<std::size_t>(found - body.begin());
            const auto redirect = [&](std::size_t offset, std::size_t length, std::size_t target) {
                const auto distance =
                    static_cast<std::ptrdiff_t>(target) - static_cast<std::ptrdiff_t>(offset + 2);
                REQUIRE(distance >= -128);
                REQUIRE(distance <= 127);
                auto* bytes = const_cast<std::uint8_t*>(code.getCode()) + offset;
                std::memset(bytes, 0x90, length);
                bytes[0] = 0xeb;
                bytes[1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(distance));
            };
            for (const auto offset : pauses) {
                const auto stub = code.getSize();
                code.inc(code.dword[r8]);
                code.cmp(code.dword[r8], 3);
                Xbyak::Label still_held;
                code.jne(still_held, code.T_NEAR);
                code.and_(code.dword[pointer], 0);
                code.L(still_held);
                code.jmp(code.getCode() + offset + 2, code.T_NEAR);
                redirect(offset, 2, stub);
            }
            const auto exchange_stub = code.getSize();
            code.inc(code.dword[r9]);
            code.xchg(code.dword[pointer], r11d);
            code.jmp(code.getCode() + exchange_offset + exchange.getSize(), code.T_NEAR);
            redirect(exchange_offset, exchange.getSize(), exchange_stub);
            code.ready();

            // Equal-size redirects preserve every original branch displacement.
            // This is a control-flow test, not a PAUSE-latency measurement.
            std::uint32_t word{1}, visits{}, attempts{};
            std::array<std::uint64_t, 4> saved{};
            using Function =
                void (*)(std::uint32_t*, std::uint64_t*, std::uint32_t*, std::uint32_t*);
            code.getCode<Function>()(&word, saved.data(), &visits, &attempts);
            CHECK(visits == (double_pause ? 4 : 3));
            CHECK(attempts == 2);
            CHECK(word == 1);
            const auto address = reinterpret_cast<std::uint64_t>(&word);
            CHECK(saved[0] == (pointer == rax ? address : Sentinels[0]));
            CHECK(saved[1] == (pointer == rbx ? address : Sentinels[1]));
            CHECK(saved[2] == (pointer == rdx ? address : Sentinels[2]));
            CHECK(saved[3] == address);
        }
    }
}

TEST_CASE("Dynarmic one and two PAUSE locks serialize six real threads",
          "[common][dynarmic_pause2][dynarmic_pause2_native]") {
    for (const bool double_pause : {false, true}) {
        CAPTURE(double_pause);
        GeneratedPauseLock generated{double_pause};
        std::uint32_t word{};
        std::atomic<unsigned> inside{}, counter{}, overlap{};
        constexpr unsigned Threads = 6, Iterations = 500;
        std::array<unsigned, Threads> completed{};
        std::array<std::uint64_t, Threads> maximum_wait{};
        std::barrier start{Threads};
        std::vector<std::thread> workers;
        for (unsigned id = 0; id < Threads; ++id) {
            workers.emplace_back([&, id] {
                start.arrive_and_wait();
                for (unsigned iteration = 0; iteration < Iterations; ++iteration) {
                    const auto began = std::chrono::steady_clock::now();
                    GeneratedGuard guard{generated, word};
                    const auto waited = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - began)
                                            .count();
                    maximum_wait[id] =
                        std::max(maximum_wait[id], static_cast<std::uint64_t>(waited));
                    if (inside.fetch_add(1, std::memory_order_relaxed) != 0) {
                        ++overlap;
                    }
                    const auto previous = counter.load(std::memory_order_relaxed);
                    if (iteration % 128 == 0) {
                        std::this_thread::yield();
                    }
                    counter.store(previous + 1, std::memory_order_relaxed);
                    inside.fetch_sub(1, std::memory_order_relaxed);
                    ++completed[id];
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        CHECK(overlap.load() == 0);
        CHECK(inside.load() == 0);
        CHECK(counter.load() == Threads * Iterations);
        CHECK(word == 0);
        for (unsigned id = 0; id < Threads; ++id) {
            CAPTURE(id);
            CHECK(completed[id] == Iterations);
            std::printf("PAUSE2=%d thread=%u completed=%u max_wait_ns=%llu (no timing threshold)\n",
                        double_pause, id, completed[id],
                        static_cast<unsigned long long>(maximum_wait[id]));
        }
    }
}

TEST_CASE("Dynarmic one and two PAUSE locks publish payload and release after unwinding",
          "[common][dynarmic_pause2][dynarmic_pause2_native]") {
    for (const bool double_pause : {false, true}) {
        CAPTURE(double_pause);
        GeneratedPauseLock generated{double_pause};
        std::uint32_t word{};
        // Relaxed atomic payload avoids C++ data races without adding hardware
        // ordering; the generated acquire/release must carry the handoff.
        std::atomic<std::uint64_t> serial{}, complement{~std::uint64_t{0}};
        std::atomic<unsigned> turn{}, bad{};
        std::barrier start{2};
        constexpr unsigned Iterations = 1000;
        std::array<std::thread, 2> workers;
        for (unsigned id = 0; id < workers.size(); ++id) {
            workers[id] = std::thread([&, id] {
                start.arrive_and_wait();
                for (unsigned completed = 0; completed < Iterations;) {
                    bool progressed{};
                    {
                        GeneratedGuard guard{generated, word};
                        if (turn.load(std::memory_order_relaxed) == id) {
                            const auto previous = serial.load(std::memory_order_relaxed);
                            if (complement.load(std::memory_order_relaxed) != ~previous) {
                                ++bad;
                            }
                            serial.store(previous + 1, std::memory_order_relaxed);
                            complement.store(~(previous + 1), std::memory_order_relaxed);
                            turn.store(id ^ 1, std::memory_order_relaxed);
                            ++completed;
                            progressed = true;
                        }
                    }
                    if (!progressed) {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        CHECK(bad.load() == 0);
        CHECK(serial.load() == 2 * Iterations);
        CHECK(word == 0);
        bool caught{};
        try {
            GeneratedGuard guard{generated, word};
            throw 17;
        } catch (int value) {
            caught = value == 17;
        }
        CHECK(caught);
        CHECK(word == 0);
        {
            GeneratedGuard guard{generated, word};
            CHECK(word == 1);
        }
        CHECK(word == 0);
    }
}

TEST_CASE("A64 same-value STXR invalidates the other processor reservation",
          "[common][dynarmic_pause2][dynarmic_pause2_exclusive]") {
    CAPTURE(Dynarmic::DoublePauseForExclusiveLock(), Dynarmic::UseWaitpkgForExclusiveLock(true));
    for (const bool inline_fastmem : {false, true}) {
        CAPTURE(inline_fastmem);
        ExclusiveArena arena;
        Dynarmic::ExclusiveMonitor monitor{2};
        ExclusiveEnvironment first{arena}, second{arena};
        Dynarmic::A64::Jit jit0{ExclusiveConfig(first, monitor, 0, inline_fastmem)};
        Dynarmic::A64::Jit jit1{ExclusiveConfig(second, monitor, 1, inline_fastmem)};
        first.jit = &jit0;
        second.jit = &jit1;
        constexpr std::uint64_t Address = 0x100, Other = 0x108, Initial = 0x123456789abcdef0;
        arena.Store(Address, Initial);
        arena.Store(Other, Initial);
        const auto load = [&](auto& jit, auto& env, std::uint64_t address) {
            ExecuteExclusive(jit, env, ExclusiveEnvironment::LoadPc, address);
            REQUIRE(jit.GetRegister(1) == Initial);
        };
        const auto store = [&](auto& jit, auto& env, std::uint64_t address, unsigned status) {
            ExecuteExclusive(jit, env, ExclusiveEnvironment::StorePc, address, Initial);
            CHECK(jit.GetRegister(4) == status);
            CHECK(arena.Load(address) == Initial);
        };
        load(jit0, first, Address);
        load(jit1, second, Address);
        store(jit1, second, Address, 0);
        store(jit0, first, Address, 1); // CAS equality alone must not permit success.
        load(jit0, first, Address);
        store(jit0, first, Address, 0); // A new reservation can succeed.
        load(jit0, first, Address);
        load(jit1, second, Other);
        store(jit1, second, Other, 0);
        store(jit0, first, Address, 0); // Preserve unrelated-address reservations.
        load(jit0, first, Address);
        monitor.ClearProcessor(0);
        store(jit0, first, Address, 1);
        load(jit0, first, Address);
        ExecuteExclusive(jit0, first, ExclusiveEnvironment::ClearPc, Address);
        store(jit0, first, Address, 1);
        if (inline_fastmem) {
            CHECK(first.reads == 0);
            CHECK(second.reads == 0);
            CHECK(first.exclusive_writes == 0);
            CHECK(second.exclusive_writes == 0);
        } else {
            CHECK(first.reads > 0);
            CHECK(second.reads > 0);
            CHECK(first.exclusive_writes > 0);
            CHECK(second.exclusive_writes > 0);
        }
    }
}
