// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <limits>
#include <span>
#include <utility>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/exception.h"

namespace VideoCommon {

using NativeShaderSourceRanges = std::vector<std::pair<DAddr, size_t>>;
inline constexpr u64 NativeShaderSourcePageSize = 0x1000;

// Failure-only: translate the pages actually touched by native capture, not an assumed large
// shader allocation. GPU-contiguous code need not have contiguous device backing.
template <typename Translate>
NativeShaderSourceRanges NativeShaderSourcePages(
    std::span<const std::pair<GPUVAddr, size_t>> source_ranges, Translate&& translate) {
    constexpr u64 Mask = NativeShaderSourcePageSize - 1;
    constexpr u64 MaxCaptureBytes = 65536 * 16 + NativeShaderSourcePageSize;
    NativeShaderSourceRanges pages;
    for (const auto& [address, size] : source_ranges) {
        if (size == 0) continue;
        if (size > MaxCaptureBytes || address > std::numeric_limits<u64>::max() - size) {
            continue;
        }
        const u64 last = (address + size - 1) & ~Mask;
        for (u64 page = address & ~Mask;; page += NativeShaderSourcePageSize) {
            if (const auto device = translate(page)) {
                pages.emplace_back(*device & ~Mask, NativeShaderSourcePageSize);
            }
            if (page == last) break;
        }
    }
    std::ranges::sort(pages);
    NativeShaderSourceRanges merged;
    for (const auto& [address, size] : pages) {
        if (!merged.empty() && address <= merged.back().first + merged.back().second) {
            auto& previous = merged.back();
            previous.second = std::max(previous.first + previous.second, address + size) -
                              previous.first;
        } else {
            merged.emplace_back(address, size);
        }
    }
    return merged;
}

// Carries source ownership through a throwing environment constructor. Keep the visible error
// unchanged; consumers still handle it as Shader::Exception. Transient trap guards are not wrapped.
class NativeShaderSourceError final : public Shader::Exception {
public:
    NativeShaderSourceError(const Shader::Exception& error, NativeShaderSourceRanges ranges_)
        : Shader::Exception{error.what()}, ranges{std::move(ranges_)} {}

    const NativeShaderSourceRanges& SourceRanges() const noexcept { return ranges; }

private:
    NativeShaderSourceRanges ranges;
};

} // namespace VideoCommon
