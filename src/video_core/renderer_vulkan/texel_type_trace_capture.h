// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <type_traits>

#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/texel_type_trace.h"
#include "video_core/surface.h"

namespace Vulkan::TexelTypeTrace {

struct Identity {
    std::uint64_t hash{}, auxiliary_hash{}, program_address{};
    std::uint32_t stage{};
    bool native{};
};

// Bindings count descriptor declarations; payload entries count array elements.
struct Offsets {
    std::uint32_t binding{};
    std::size_t entry{};

    void SkipStage(const Shader::Info& info) noexcept {
        binding += static_cast<std::uint32_t>(info.constant_buffer_descriptors.size() +
                                             info.storage_buffers_descriptors.size() +
                                             info.texture_buffer_descriptors.size() +
                                             info.image_buffer_descriptors.size() +
                                             info.texture_descriptors.size() +
                                             info.image_descriptors.size());
        entry += Shader::NumDescriptors(info.constant_buffer_descriptors) +
                 Shader::NumDescriptors(info.storage_buffers_descriptors) +
                 Shader::NumDescriptors(info.texture_buffer_descriptors) +
                 Shader::NumDescriptors(info.image_buffer_descriptors) +
                 Shader::NumDescriptors(info.texture_descriptors) +
                 Shader::NumDescriptors(info.image_descriptors);
    }

    void SkipBuffers(const Shader::Info& info) noexcept {
        binding += static_cast<std::uint32_t>(info.constant_buffer_descriptors.size() +
                                             info.storage_buffers_descriptors.size());
        entry += Shader::NumDescriptors(info.constant_buffer_descriptors) +
                 Shader::NumDescriptors(info.storage_buffers_descriptors);
    }
};

inline Numeric PixelClass(VideoCore::Surface::PixelFormat format) noexcept {
    using namespace VideoCore::Surface;
    return Classify(format != PixelFormat::Invalid, IsPixelFormatInteger(format),
                    IsPixelFormatSignedInteger(format));
}

template <class Descriptor>
void Capture(Scope& scope, const Shader::Info& info, const Identity& identity,
             const Offsets& offsets, const Descriptor& desc, std::uint32_t element,
             std::uint32_t requested_tic, std::uint32_t resolved_tic,
             VideoCore::Surface::PixelFormat guest_format,
             VideoCore::Surface::PixelFormat selected_format, std::uint64_t guest_address,
             std::uint32_t guest_size, std::uint32_t view_type,
             Numeric compiled_override = Numeric::Unknown) noexcept {
    constexpr bool storage = std::is_same_v<Descriptor, Shader::ImageBufferDescriptor>;
    Row row{};
    row.kind = storage ? Kind::Storage : Kind::Uniform;
    row.compiled = Numeric::Float;
    row.compiled_format = ~std::uint32_t{};
    row.read = true;
    if constexpr (storage) {
        row.compiled = compiled_override != Numeric::Unknown
                           ? compiled_override
                           : (desc.is_integer ? Numeric::Uint : Numeric::Float);
        row.compiled_format = static_cast<std::uint32_t>(desc.format);
        row.read = desc.is_read;
        row.written = desc.is_written;
    }
    row.handle_source = desc.handle_program != ~std::uint32_t{} ? HandleSource::Computed
                                                               : HandleSource::ConstantBuffer;
    if constexpr (!storage) {
        if (desc.has_secondary && row.handle_source != HandleSource::Computed) {
            row.handle_source = HandleSource::SecondaryConstantBuffer;
        }
        row.secondary_cbuf = desc.secondary_cbuf_index;
        row.secondary_offset = desc.secondary_cbuf_offset;
    }
    row.shader_hash = identity.hash;
    row.auxiliary_shader_hash = identity.auxiliary_hash;
    row.program_address = identity.program_address;
    row.stage = identity.stage;
    row.native = identity.native;
    row.entry = offsets.entry + element;
    row.binding = offsets.binding;
    row.element = element;
    row.count = desc.count;
    row.size_shift = desc.size_shift;
    row.cbuf = desc.cbuf_index;
    row.cbuf_offset = desc.cbuf_offset;
    row.handle_program = desc.handle_program;
    row.requested_tic = requested_tic;
    row.resolved_tic = resolved_tic;
    row.guest_class = PixelClass(guest_format);
    row.selected_class = PixelClass(selected_format);
    row.guest_format = static_cast<std::uint32_t>(guest_format);
    row.selected_format = static_cast<std::uint32_t>(selected_format);
    row.guest_address = guest_address;
    row.guest_size = guest_size;
    row.view_type = view_type;
    row.stage_image_atomic = info.uses_atomic_image_u32;
    scope.Add(row);
}

} // namespace Vulkan::TexelTypeTrace
