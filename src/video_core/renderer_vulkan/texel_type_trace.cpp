// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging.h"
#include "video_core/renderer_vulkan/texel_type_trace.h"

namespace Vulkan::TexelTypeTrace {
namespace {
const char* Name(Numeric value) noexcept {
    switch (value) {
    case Numeric::Float:
        return "FLOAT";
    case Numeric::Uint:
        return "UINT";
    case Numeric::Sint:
        return "SINT";
    default:
        return "UNKNOWN";
    }
}
} // namespace

void Report(const std::shared_ptr<const Batch>& batch, std::uint64_t pipeline, bool usable,
            bool descriptor_buffer) noexcept {
    if (!batch || !batch->ClaimReport()) {
        return;
    }
    for (std::size_t i = 0; i < batch->size; ++i) {
        const auto& row = batch->rows[i];
        try {
            LOG_INFO(Render_Vulkan,
                     "TexelTypeTrace pipeline={:#x} usable={} descriptor_buffer={} "
                     "kind={} stage={} shader={:016x} auxiliary_shader={:016x} native={} "
                     "program={:#x} set=0 binding={} element={}/{} entry={} source={} "
                     "cb={} offset={:#x} size_shift={} secondary_cb={} secondary_offset={:#x} "
                     "handle_program={:#x} requested_tic={} resolved_tic={} view_type={} "
                     "compiled={} compiled_format={} guest_class={} guest_format={} "
                     "selected_class={} selected_format={} vk_format={} mismatch_prediction={} "
                     "guest_address={:#x} guest_size={:#x} host_observed={} view={:#x} "
                     "bind_offset={:#x} bind_size={:#x} read={} written={} "
                     "atomic_descriptor=unknown image_atomic_stage={} "
                     "uniform_family_observed={} uniform_assessed_stage={} uniform_family_reason={} "
                     "uniform_policy_evaluated={} uniform_policy_eligible={} "
                     "uniform_declarations={} uniform_target_count={} uniform_offending_opcode={} "
                     "uniform_features_queried={} uniform_format_mask={:#x} "
                     "uniform_snapshot={} uniform_family_built={} "
                     "budget={}/{} row_cap={} row_cap_hits={} allocation_failures={} "
                     "abandoned_batches={} coverage=bounded_configure_not_execution",
                     pipeline, usable, descriptor_buffer,
                     row.kind == Kind::Storage ? "storage" : "uniform", row.stage,
                     row.shader_hash, row.auxiliary_shader_hash, row.native, row.program_address,
                     row.binding, row.element, row.count, row.entry,
                     static_cast<std::uint32_t>(row.handle_source), row.cbuf, row.cbuf_offset,
                     row.size_shift, row.secondary_cbuf, row.secondary_offset, row.handle_program,
                     row.requested_tic, row.resolved_tic, row.view_type, Name(row.compiled),
                     row.compiled_format, Name(row.guest_class), row.guest_format,
                     Name(row.selected_class), row.selected_format, row.vk_format, row.mismatch,
                     row.guest_address, row.guest_size, row.host_observed, row.buffer_view,
                     row.host_offset, row.host_size, row.read, row.written, row.stage_image_atomic,
                     row.uniform_family.observed, row.uniform_family.assessed_stage,
                     UniformFamilyReasonName(row.uniform_family.reason),
                     row.uniform_family.policy_evaluated, row.uniform_family.policy_eligible,
                     row.uniform_family.declaration_count, row.uniform_family.target_count,
                     row.uniform_family.offending_opcode,
                     row.uniform_family.features_queried, row.uniform_family.supported_format_mask,
                     row.uniform_family.snapshot_available, row.uniform_family.family_built,
                     row.ticket, row.limit, Batch::RowLimit, batch->row_cap_hits,
                     budget.allocation_failures.load(std::memory_order_relaxed),
                     budget.abandoned_batches.load(std::memory_order_relaxed));
        } catch (...) {
            // A diagnostic logging failure must not change command recording.
        }
    }
}

} // namespace Vulkan::TexelTypeTrace
