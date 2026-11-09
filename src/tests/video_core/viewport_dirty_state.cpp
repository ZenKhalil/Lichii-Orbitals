// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"

namespace {

using Maxwell3D = Tegra::Engines::Maxwell3D;
using Tables = Maxwell3D::DirtyState::Tables;

// Inspect the complete production builder's result, including both dirty tables.
// No dirty-setup implementation, register writer or viewport formula is copied.
bool Marks(const Tables& tables, std::size_t method, u8 flag) {
    for (const auto& table : tables) {
        if (table[method] == flag) {
            return true;
        }
    }
    return false;
}

struct RegisterRange {
    const char* name;
    std::size_t first;
    std::size_t words;
};

} // namespace

TEST_CASE("Vulkan viewport depth mode has a dirty dependency",
          "[video_core][viewport][viewport_dirty_state]") {
    Tables tables{};
    Vulkan::StateTracker::SetupTables(tables);

    // GetViewportState changes minDepth for this register even if no transform,
    // rectangle or other dynamic-state register is written between draws.
    CHECK(Marks(tables, MAXWELL3D_REG_INDEX(depth_mode), Vulkan::Dirty::Viewports));
}

TEST_CASE("Vulkan viewport dependencies cover complete consumed register ranges",
          "[video_core][viewport][viewport_dirty_state]") {
    Tables tables{};
    Vulkan::StateTracker::SetupTables(tables);
    constexpr std::array ranges{
        RegisterRange{"viewport_transform", MAXWELL3D_REG_INDEX(viewport_transform),
                      sizeof(Maxwell3D::Regs::viewport_transform) / sizeof(u32)},
        RegisterRange{"viewports", MAXWELL3D_REG_INDEX(viewports),
                      sizeof(Maxwell3D::Regs::viewports) / sizeof(u32)},
        RegisterRange{"surface_clip", MAXWELL3D_REG_INDEX(surface_clip),
                      sizeof(Maxwell3D::Regs::surface_clip) / sizeof(u32)},
        RegisterRange{"window_origin", MAXWELL3D_REG_INDEX(window_origin), 1},
        RegisterRange{"viewport_scale_offset_enabled",
                      MAXWELL3D_REG_INDEX(viewport_scale_offset_enabled), 1},
    };

    // Every transform slot includes XYZ scales/translations and swizzles.
    // Both surface_clip words matter: x/width and y/height supply the disabled
    // transform branch, and height also affects the lower-left origin branch.
    // Preserve the existing full viewports dependency used with scissors.
    for (const auto& range : ranges) {
        CAPTURE(range.name);
        for (std::size_t word = 0; word < range.words; ++word) {
            const auto method = range.first + word;
            CAPTURE(word, method);
            CHECK(Marks(tables, method, Vulkan::Dirty::Viewports));
        }
    }
}

TEST_CASE("Unrelated Vulkan dynamic state does not dirty viewport state",
          "[video_core][viewport][viewport_dirty_state]") {
    Tables tables{};
    Vulkan::StateTracker::SetupTables(tables);
    constexpr std::array controls{
        RegisterRange{"scissor_test", MAXWELL3D_REG_INDEX(scissor_test),
                      sizeof(Maxwell3D::Regs::scissor_test) / sizeof(u32)},
        RegisterRange{"blend_color", MAXWELL3D_REG_INDEX(blend_color),
                      sizeof(Maxwell3D::Regs::blend_color) / sizeof(u32)},
        RegisterRange{"depth_test_enable", MAXWELL3D_REG_INDEX(depth_test_enable), 1},
    };
    constexpr std::array<u8, 3> expected{Vulkan::Dirty::Scissors, Vulkan::Dirty::BlendConstants,
                                         Vulkan::Dirty::DepthTestEnable};
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const auto& range = controls[index];
        CAPTURE(range.name);
        for (std::size_t word = 0; word < range.words; ++word) {
            const auto method = range.first + word;
            CAPTURE(word, method);
            CHECK(Marks(tables, method, expected[index]));
            CHECK_FALSE(Marks(tables, method, Vulkan::Dirty::Viewports));
        }
    }
}

TEST_CASE("Vulkan viewport invalidation is consumed independently of scissors",
          "[video_core][viewport][viewport_dirty_state]") {
    Vulkan::StateTracker tracker;
    CHECK_FALSE(tracker.TouchViewports());
    tracker.InvalidateViewports();
    CHECK(tracker.TouchViewports());
    CHECK_FALSE(tracker.TouchViewports());
    tracker.InvalidateScissors();
    CHECK_FALSE(tracker.TouchViewports());
    CHECK(tracker.TouchScissors());
    CHECK_FALSE(tracker.TouchScissors());
    tracker.InvalidateCommandBufferState();
    CHECK(tracker.TouchViewports());
    CHECK_FALSE(tracker.TouchViewports());
}
