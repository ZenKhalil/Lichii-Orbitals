// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace VideoCommon::NativeShaderFailureDump {

// Diagnostic only: call on failed lowering with the vector/header already in memory.
// Never re-read guest memory, change the input, or replace the caller's original exception.
struct Input {
    std::span<const std::byte> native_code;
    std::span<const std::byte> captured_header;
    std::span<const std::byte> verified_traps;
    std::uint64_t program_base;
    std::uint32_t native_header_skip;
    std::uint32_t captured_header_offset;
    std::uint32_t stage;
    std::uint32_t tessellation_stage;
    bool geometry;
    bool tensor;
    std::uint32_t error_offset;
    std::string_view reason;
};

enum class Status { Disabled, LimitReached, Written, Failed };
struct Result {
    Status status;
    std::filesystem::path directory;
};

inline void WriteJsonString(std::ostream& out, std::string_view value) {
    constexpr char Hex[] = "0123456789abcdef";
    out.put('"');
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out.put('\\');
            out.put(static_cast<char>(c));
        } else if (c < 0x20) {
            out << "\\u00" << Hex[c >> 4] << Hex[c & 15];
        } else {
            out.put(static_cast<char>(c));
        }
    }
    out.put('"');
}

inline Result Capture(const Input& input) noexcept {
    try {
        // Cached on the first failure. Successful lowerings do not call this helper.
        static const std::string root_text = [] {
            const char* value = std::getenv("EDEN_NX2_SHADER_FAILURE_DUMP");
            return value ? std::string{value} : std::string{};
        }();
        if (root_text.empty()) {
            return {Status::Disabled, {}};
        }
        // Failed I/O attempts also consume a slot. Four failures, at most 5 MiB + 320 binary bytes
        // per process (4 * (65536 native instructions + 65536 trap offsets + 80 header bytes)).
        static std::atomic<unsigned> attempts{};
        constexpr unsigned MaxCaptures = 4;
        unsigned slot = attempts.load(std::memory_order_relaxed);
        do {
            if (slot >= MaxCaptures) {
                return {Status::LimitReached, {}};
            }
        } while (!attempts.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed));

        const std::filesystem::path root{root_text};
        constexpr std::size_t MaxInstructions = 65536;
        if (!root.is_absolute() || input.native_code.empty() ||
            input.native_code.size() > MaxInstructions * 16 ||
            input.native_code.size() % 16 != 0 ||
            (input.captured_header.size() != 0 && input.captured_header.size() != 80) ||
            input.verified_traps.size() > MaxInstructions * sizeof(std::uint32_t) ||
            input.verified_traps.size() % sizeof(std::uint32_t) != 0 ||
            input.reason.size() > 4096) {
            return {Status::Failed, {}};
        }
        std::filesystem::create_directories(root);
        static const std::string session =
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::path directory;
        // Atomic directory creation reserves a fresh location. Existing captures are never
        // overwritten, even across processes or the unlikely event of a session-name collision.
        for (unsigned collision = 0; collision < 8; ++collision) {
            auto candidate = root / ("failure-" + session + "-" + std::to_string(slot) + "-" +
                                     std::to_string(collision));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                break;
            }
        }
        if (directory.empty()) {
            return {Status::Failed, {}};
        }
        const auto write = [&](const char* name, std::span<const std::byte> bytes) {
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(directory / name, std::ios::binary);
            if (!bytes.empty()) {
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            }
            out.close();
        };
        write("native-code.bin", input.native_code);
        if (!input.captured_header.empty()) {
            write("environment-header.bin", input.captured_header);
        }
        write("verified-traps.bin", input.verified_traps);
        // Metadata is written last; only a complete JSON document marks a completed capture.
        // A partial directory on failure is retained for inspection and never reused.
        std::ofstream meta;
        meta.exceptions(std::ios::failbit | std::ios::badbit);
        meta.open(directory / "metadata.json", std::ios::binary);
        meta << "{\n  \"schema\": 1,\n  \"event\": \"native_lowering_failed\",\n"
                "  \"native_code_origin\": \"exact vector passed to LowerToMaxwell\",\n"
                "  \"header_origin\": \"existing environment sph; no guest-memory reread\",\n"
                "  \"unread_native_header_extension_included\": false,\n"
                "  \"program_base\": " << input.program_base <<
                ",\n  \"native_header_skip\": " << input.native_header_skip <<
                ",\n  \"native_code_address\": " << input.program_base + input.native_header_skip <<
                ",\n  \"captured_header_offset\": " << input.captured_header_offset <<
                ",\n  \"captured_header_bytes\": " << input.captured_header.size() <<
                ",\n  \"native_code_bytes\": " << input.native_code.size() <<
                ",\n  \"verified_trap_count\": " << input.verified_traps.size() / 4 <<
                ",\n  \"stage\": " << input.stage <<
                ",\n  \"tessellation_stage\": " << input.tessellation_stage <<
                ",\n  \"geometry\": " << (input.geometry ? "true" : "false") <<
                ",\n  \"tensor\": " << (input.tensor ? "true" : "false") <<
                ",\n  \"error_offset\": " << input.error_offset << ",\n  \"reason\": ";
        WriteJsonString(meta, input.reason);
        meta << "\n}\n";
        meta.close();
        return {Status::Written, std::move(directory)};
    } catch (...) {
        // Diagnostics must not hide/replace the shader rejection, including on allocation,
        // filesystem or permission failure. No retries beyond the bounded attempt budget.
        return {Status::Failed, {}};
    }
}

} // namespace VideoCommon::NativeShaderFailureDump
