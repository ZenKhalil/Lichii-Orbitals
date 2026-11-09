// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Common::Nx2AudioCaller {
inline bool ParseEnabled(const char* value) {
    return value && value[0] == '1' && value[1] == '\0';
}
inline bool Enabled() {
    static const bool enabled = ParseEnabled(std::getenv("EDEN_NX2_AUDIO_CALLER_TRACE"));
    return enabled;
}

struct Budget {
    static constexpr unsigned MaxAttempts = 64;
    static constexpr unsigned MaxCaptures = 8;
    std::atomic<unsigned> attempts{}, captures{};
    unsigned Begin() {
        if (captures.load(std::memory_order_relaxed) >= MaxCaptures) {
            return 0;
        }
        // Saturate rather than wrapping if a title polls indefinitely.
        auto n = attempts.load(std::memory_order_relaxed);
        while (n < MaxAttempts) {
            if (attempts.compare_exchange_strong(n, n + 1, std::memory_order_relaxed)) {
                return n + 1;
            }
        }
        return 0;
    }
    bool Capture() {
        auto n = captures.load(std::memory_order_relaxed);
        while (n < MaxCaptures) {
            if (captures.compare_exchange_strong(n, n + 1, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }
};

inline std::string Hex(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    return out;
}

inline bool CheckedRange(std::uint64_t address, std::size_t size) {
    constexpr std::uint64_t GuestLimit = std::uint64_t{1} << 48;
    return address != 0 && size != 0 && address < GuestLimit && size <= GuestLimit - address;
}

// Reader returns success only for the full requested range; it must not flush GPU memory.
// No addresses in this artifact are adjusted: caller PCs are saved return PCs, not sampled PCs.
template <typename Reader>
nlohmann::json CaptureMemory(std::uint64_t pc, std::uint64_t lr, std::uint64_t fp,
                             std::uint64_t sp, Reader&& read) {
    nlohmann::json out{{"pc", pc}, {"lr", lr}, {"fp", fp}, {"sp", sp},
                       {"frames", nlohmann::json::array()},
                       {"code_windows", nlohmann::json::array()},
                       {"memory_semantics", "best-effort CPU backing, non-flushing; mappings and other threads are not frozen"},
                       {"frame_semantics", "pc is saved client PC; lr and frame return_pc are original saved return PCs"}};
    std::vector<std::uint64_t> pcs{pc, lr};
    std::string stop = "frame_limit";
    for (unsigned i = 0; i < 16; ++i) {
        if (!fp) { stop = "null_fp"; break; }
        if (fp & 15) { stop = "unaligned_fp"; break; }
        if (!CheckedRange(fp, 16)) { stop = "overflow_fp"; break; }
        std::array<std::uint64_t, 2> frame{};
        if (!read(fp, frame.data(), sizeof(frame))) { stop = "unmapped_or_failed_frame"; break; }
        out["frames"].push_back({{"address", fp}, {"next_fp", frame[0]},
                                 {"return_pc", frame[1]}});
        pcs.push_back(frame[1]);
        if (!frame[0]) { stop = "null_fp"; break; }
        if (frame[0] <= fp) { stop = "non_increasing_fp"; break; }
        fp = frame[0];
    }
    out["frame_stop"] = stop;
    std::vector<std::uint64_t> seen;
    for (const auto original_pc : pcs) {
        if (std::find(seen.begin(), seen.end(), original_pc) != seen.end()) {
            continue;
        }
        seen.push_back(original_pc);
        // At most PC+LR+16 frame return addresses =18 windows, each256 bytes.
        nlohmann::json window{{"original_pc", original_pc}, {"success", false}};
        if (original_pc >= 128 && !(original_pc & 3)) {
            const auto start = original_pc - 128;
            std::array<std::uint8_t, 256> bytes{};
            window["address"] = start;
            if (CheckedRange(start, bytes.size()) && read(start, bytes.data(), bytes.size())) {
                window["success"] = true;
                window["bytes_hex"] = Hex(bytes);
            }
        }
        out["code_windows"].push_back(std::move(window));
    }
    return out;
}

// A newly created directory is the ownership claim; never overwrite an earlier artifact.
inline bool WriteArtifact(const std::filesystem::path& root, unsigned attempt,
                          const nlohmann::json& record, std::filesystem::path& output) noexcept {
    output.clear();
    try {
        std::filesystem::create_directories(root);
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto dir = root / (std::to_string(stamp) + "-" + std::to_string(attempt));
        if (!std::filesystem::create_directory(dir)) {
            return false;
        }
        const auto path = dir / "caller.json";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file << record.dump(2) << '\n';
        file.close();
        output = path;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace Common::Nx2AudioCaller
