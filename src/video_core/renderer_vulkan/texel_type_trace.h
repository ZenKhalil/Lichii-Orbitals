// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace Vulkan::TexelTypeTrace {

enum class Numeric : std::uint32_t { Unknown, Float, Uint, Sint };
enum class Kind : std::uint32_t { Uniform, Storage };
enum class HandleSource : std::uint32_t { ConstantBuffer, SecondaryConstantBuffer, Computed };

constexpr bool IsEnabledValue(const char* value) noexcept {
    return value && value[0] == '1' && value[1] == '\0';
}

inline bool Enabled() noexcept {
    static const bool enabled = IsEnabledValue(std::getenv("EDEN_NX2_TEXEL_TYPE_TRACE"));
    return enabled;
}

constexpr Numeric Classify(bool valid, bool integer, bool signed_integer) noexcept {
    return !valid ? Numeric::Unknown
                  : integer ? (signed_integer ? Numeric::Sint : Numeric::Uint) : Numeric::Float;
}

constexpr bool Mismatch(Numeric compiled, Numeric selected) noexcept {
    return compiled != Numeric::Unknown && selected != Numeric::Unknown && compiled != selected;
}

template <class T>
std::uint64_t Handle(T value) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(value);
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

struct Row {
    Kind kind{};
    Numeric compiled{}, guest_class{}, selected_class{};
    HandleSource handle_source{};
    std::uint64_t shader_hash{}, auxiliary_shader_hash{}, program_address{};
    std::uint64_t guest_address{};
    std::uint64_t buffer_view{}, host_offset{}, host_size{};
    std::size_t entry{};
    std::uint32_t stage{}, binding{}, element{}, count{}, size_shift{};
    std::uint32_t cbuf{}, cbuf_offset{}, secondary_cbuf{}, secondary_offset{}, handle_program{};
    std::uint32_t requested_tic{}, resolved_tic{}, guest_format{}, selected_format{}, vk_format{};
    std::uint32_t guest_size{}, view_type{}, compiled_format{}, ticket{}, limit{};
    bool native{}, read{}, written{}, stage_image_atomic{}, host_observed{}, mismatch{};
};

struct Budget {
    static constexpr std::uint32_t MismatchLimit = 64;
    static constexpr std::uint32_t ControlLimit = 8;
    // Graphics (stages 0-4) and compute (stage 5) each have independent budgets
    // per kind: mismatches and matching/unknown controls.
    std::array<std::atomic<std::uint32_t>, 8> counts{};
    std::atomic<std::uint32_t> allocation_failures{}, abandoned_batches{};

    bool Available() const noexcept {
        for (std::size_t i = 0; i < counts.size(); ++i) {
            if (counts[i].load(std::memory_order_relaxed) < Limit(i)) {
                return true;
            }
        }
        return false;
    }

    static constexpr std::uint32_t Limit(std::size_t index) noexcept {
        return (index & 1) ? MismatchLimit : ControlLimit;
    }

    bool Reserve(Row& row) noexcept {
        const auto index = 4 * static_cast<std::size_t>(row.stage == 5) +
                           2 * static_cast<std::size_t>(row.kind) + row.mismatch;
        const auto limit = Limit(index);
        if (counts[index].load(std::memory_order_relaxed) >= limit) {
            return false;
        }
        const auto ticket = counts[index].fetch_add(1, std::memory_order_relaxed);
        if (ticket >= limit) {
            return false;
        }
        row.ticket = ticket + 1;
        row.limit = limit;
        return true;
    }
};

inline Budget budget;

struct Batch {
    static constexpr std::size_t RowLimit = 16;
    std::array<Row, RowLimit> rows{};
    std::size_t size{};
    std::uint32_t row_cap_hits{};
    mutable std::atomic_flag reported = ATOMIC_FLAG_INIT;

    bool ClaimReport() const noexcept {
        return !reported.test_and_set(std::memory_order_relaxed);
    }
};

class Scope;
inline thread_local Scope* current{};

// Capture is thread-local; only a sealed, immutable batch crosses to the scheduler.
// No logging, driver calls or guest reads occur in these hooks.
class Scope {
public:
    explicit Scope(const void* queue, bool supported = true, bool enabled = Enabled(),
                   Budget& budget_ = budget) noexcept
        : previous{current}, queue_identity{queue}, owner_budget{budget_},
          active{enabled && supported && budget_.Available()} {
        current = active ? this : nullptr;
    }

    ~Scope() {
        if (batch) {
            owner_budget.abandoned_batches.fetch_add(1, std::memory_order_relaxed);
        }
        current = previous;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    bool Active() const noexcept {
        return active;
    }

    void Add(Row row) noexcept {
        if (!active) {
            return;
        }
        if (batch && batch->size == Batch::RowLimit) {
            ++batch->row_cap_hits;
            return;
        }
        row.mismatch = Mismatch(row.compiled, row.selected_class);
        if (!owner_budget.Reserve(row)) {
            return;
        }
        try {
            if (!batch) {
                batch = std::make_shared<Batch>();
            }
            batch->rows[batch->size++] = row;
        } catch (...) {
            owner_budget.allocation_failures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    static void Host(const void* queue, std::size_t entry, std::uint64_t view,
                     std::uint64_t offset, std::uint64_t size, std::uint32_t format) noexcept {
        if (!current || current->queue_identity != queue || !current->batch) {
            return;
        }
        for (std::size_t i = 0; i < current->batch->size; ++i) {
            auto& row = current->batch->rows[i];
            if (row.entry == entry) {
                row.host_observed = true;
                row.buffer_view = view;
                row.host_offset = offset;
                row.host_size = size;
                row.vk_format = format;
            }
        }
    }

    static std::shared_ptr<const Batch> SealCurrent() noexcept {
        if (!current) {
            return {};
        }
        current->active = false;
        return std::move(current->batch);
    }

private:
    Scope* previous{};
    const void* queue_identity{};
    Budget& owner_budget;
    bool active{};
    std::shared_ptr<Batch> batch;
};

void Report(const std::shared_ptr<const Batch>& batch, std::uint64_t pipeline, bool usable,
            bool descriptor_buffer) noexcept;

} // namespace Vulkan::TexelTypeTrace
