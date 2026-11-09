// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace Nx2Sampler {

// EDEN_NX2_SAMPLE=<thread names>: a sampling profiler for named host threads (Windows only).
// Names are comma separated; a name ending in '*' matches by prefix (CPUCore_*). From
// EDEN_NX2_SAMPLE_START seconds (default 60) for EDEN_NX2_SAMPLE_SECONDS (default 30), each
// thread is paused about 1000 times a second and its call stack recorded; the counts per
// function (where the thread is, and everything on its stack) go to EDEN_NX2_SAMPLE_OUT
// (default nx2_samples.txt).
void StartFromEnvironment();

struct RawSelfResult {
    std::string file;
    std::string error;
};

// The raw stack sidecar uses the same file/error result shape.
using RawStacksResult = RawSelfResult;

/// Samples the named threads for `seconds` now (blocking), writes the report to `out` (and
/// the folded stacks to `out`.folded) and returns the report.
// Passing raw_self requests a bounded .self.json sidecar with complete-or-explicitly-truncated
// valid RIP counts. Passing raw_stacks exports exact captured stacks to .stacks.json;
// this does not guarantee a complete unwind. Both defaults preserve existing output.
std::string Sample(const char* names, double seconds, const std::string& out,
                   RawSelfResult* raw_self = nullptr, RawStacksResult* raw_stacks = nullptr);

} // namespace Nx2Sampler
