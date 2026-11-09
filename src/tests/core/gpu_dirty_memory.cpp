// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <set>
#include <array>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/gpu_dirty_memory_manager.h"

TEST_CASE("GPUDirtyMemoryManager reports collected writes until they are gathered",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    manager.Collect(0x10040, 0x10);
    manager.Collect(0x20000, 0x40);

    REQUIRE(manager.Overlaps(0x10040, 1));
    REQUIRE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE(manager.Overlaps(0x20000, 4));
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x10080, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x30000, 0x1000));

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        gathered.emplace_back(address, size);
    };
    manager.Gather(callback);

    REQUIRE(gathered.size() == 2);
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE_FALSE(manager.Overlaps(0x20000, 4));
}

TEST_CASE("GPU dirty write collection covers complete arbitrary byte intervals",
          "[core][gpu_dirty_memory][collector_boundaries]") {
    constexpr std::array<std::pair<size_t, size_t>, 12> cases{{
        {0, 0x1000}, {0x300, 0xd00}, {0x680, 0x980},
        {0x7f8, 16}, {0, 0}, {0x301, 0}, {0x7ff, 1}, {0x7ff, 2},
        {0xb00, 0x500}, {0, 0x800}, {0x800, 0x800}, {0x41, 0x2001}}};
    const auto index = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    const auto [offset, size] = cases[index];
    CAPTURE(offset, size);
    Core::GPUDirtyMemoryManager manager;
    const PAddr address = 0x20000 + offset;
    manager.Collect(address, size);
    const auto coverage = [&](bool gather) {
        std::set<PAddr> lines;
        std::function<void(PAddr, size_t)> collect = [&](PAddr start, size_t count) {
            for (PAddr at = start; at < start + count; at += 64) {
                lines.insert(at);
            }
        };
        if (gather) {
            manager.Gather(collect);
        } else {
            manager.ForEachPending(collect);
        }
        return lines;
    };
    std::set<PAddr> expected;
    if (size != 0) {
        for (PAddr at = address & ~PAddr{63}; at < address + size; at += 64) {
            expected.insert(at);
        }
    }
    CHECK(coverage(false) == expected);
    CHECK(coverage(true) == expected);
    CHECK(coverage(false).empty());
    // Reuse after Gather must not retain previous pages or omit a new crossing write.
    manager.Collect(0x307f8, 16);
    expected = {0x307c0, 0x30800};
    CHECK(coverage(false) == expected);
    CHECK(coverage(true) == expected);
    CHECK(coverage(false).empty());
}
