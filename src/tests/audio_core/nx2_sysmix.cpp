// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/atomic_ops.h"
#include "common/scope_exit.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/audio/errors.h"
#include "core/hle/service/audio/nx2_audio_device_manager.h"
#include "core/hle/service/audio/nx2_sysmix.h"
#include "tests/audio_core/sysmix_test_context.h"

using Service::Audio::SysmixOutput;
using SysmixTests::Context;

TEST_CASE("sysmix input accepts a ring without output timestamp storage", "[sysmix][input]") {
    Context context;
    SysmixOutput stream{context.system, *context.process, context.sink, 2,
                        Context::Base, 0x2000, 2, true};
    REQUIRE(stream.Open(2, 1, 48000, 2000, 0, 0).IsSuccess());
}

TEST_CASE("sysmix input publishes fresh mono capture across ring wrap", "[sysmix][input]") {
    Context context;
    const auto previous_gain = Settings::values.microphone_volume.GetValue();
    Settings::values.microphone_volume.SetValue(100);
    SCOPE_EXIT {
        Settings::values.microphone_volume.SetValue(previous_gain);
    };
    const u32 cursor = 8;
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x44, &cursor, sizeof(cursor)));
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1004, &cursor, sizeof(cursor)));
    SysmixOutput stream{
        context.system, *context.process, context.sink, 2, Context::Base, Context::Size, 2, true};
    REQUIRE(stream.IsInput());
    REQUIRE(stream.Open(2, 1, 48000, 8, 0, 0).IsSuccess());
    REQUIRE(stream.SetDataNotification(2, 2).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    u32 write{};
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x1004, write));
    REQUIRE(write == 8);
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    const std::array<s16, 6> samples{1000, 3000, -1000, -3000, 100, 300};
    context.sink.last->ProcessAudioIn(samples, 3);
    stream.Pump();
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x1004, write));
    REQUIRE(write == 2);
    u32 read{};
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x44, read));
    REQUIRE(read == 8);
    s16 last{};
    std::array<s16, 2> first{};
    REQUIRE(context.Memory().ReadBlock(Context::Base + 0x1040 + 8 * 2, &last, sizeof(last)));
    REQUIRE(context.Memory().ReadBlock(Context::Base + 0x1040, first.data(), sizeof(first)));
    REQUIRE(last == 2000);
    REQUIRE(first == std::array<s16, 2>{-2000, 200});
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.GetCounters().accepted_frames == 3);
    stream.Pump();
    REQUIRE(stream.GetCounters().accepted_frames == 3);
    u32 purged{};
    REQUIRE(stream.Purge(0, purged).IsSuccess());
    REQUIRE(purged == 3);
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x44, read));
    REQUIRE(read == write);
}

TEST_CASE("sysmix input applies gain to float capture and protects producer ownership",
          "[sysmix][input]") {
    Context context;
    const auto previous_gain = Settings::values.microphone_volume.GetValue();
    Settings::values.microphone_volume.SetValue(100);
    SCOPE_EXIT {
        Settings::values.microphone_volume.SetValue(previous_gain);
    };
    SysmixOutput stream{
        context.system, *context.process, context.sink, 2, Context::Base, Context::Size, 2, true};
    REQUIRE(stream.Open(5, 2, 48000, 8, 0, 0).IsSuccess());
    REQUIRE(stream.SetGain(0.5f, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    const std::array<s16, 2> samples{16384, -16384};
    context.sink.last->ProcessAudioIn(samples, 1);
    stream.Pump();
    std::array<f32, 2> recorded{};
    REQUIRE(context.Memory().ReadBlock(Context::Base + 0x1040, recorded.data(), sizeof(recorded)));
    REQUIRE(recorded == std::array<f32, 2>{0.25f, -0.25f});
    const u32 tampered = 2;
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1004, &tampered, sizeof(tampered)));
    stream.Pump();
    REQUIRE(stream.GetState() == 0);
    REQUIRE(stream.GetCounters().faults == 1);
}

TEST_CASE("sysmix input purge preserves capture during concurrent guest consumption",
          "[sysmix][input]") {
    Context context;
    constexpr u32 frames = 2048;
    SysmixOutput stream{
        context.system, *context.process, context.sink, 2, Context::Base, Context::Size, 2, true};
    REQUIRE(stream.Open(2, 2, 48000, frames, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    auto* read_cursor = reinterpret_cast<u32_le*>(context.backing + 0x44);
    auto* write_cursor = reinterpret_cast<u32_le*>(context.backing + 0x1004);
    std::jthread guest{[&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            u32_le read{}, write{};
            static_cast<void>(Common::AtomicCompareAndSwap(read_cursor, u32_le{0}, u32_le{0}, read));
            static_cast<void>(Common::AtomicCompareAndSwap(write_cursor, u32_le{0}, u32_le{0}, write));
            if (read != write) {
                static_cast<void>(Common::AtomicCompareAndSwap(
                    read_cursor, u32_le{(static_cast<u32>(read) + 1) % (frames + 1)}, read));
            }
        }
    }};
    const std::array<s16, 480> samples{};
    for (u32 iteration = 0; iteration < 4096; ++iteration) {
        context.sink.last->ProcessAudioIn(samples, 240);
        stream.Pump();
        u32 purged = 99;
        const auto result = stream.Purge(iteration % 2 == 0 ? 1 : 0, purged);
        REQUIRE(stream.GetState() == 2);
        REQUIRE(stream.GetCounters().faults == 0);
        if (result.IsFailure()) {
            REQUIRE(result == Service::Audio::ResultOperationFailed);
            REQUIRE(purged == 0);
        }
    }
    REQUIRE(stream.GetCounters().accepted_frames > 0);
}

namespace {
void Cursor(Context& context, u32 read, u32 write) {
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x44, &read, sizeof(read)));
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1004, &write, sizeof(write)));
}
u32 ReadCursor(Context& context) {
    u32 read{};
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x44, read));
    return read;
}
void Sample(Context& context, u32 slot, f32 left, f32 right) {
    const std::array<f32, 2> frame{left, right};
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040 + u64{slot} * 8,
                                        frame.data(), sizeof(frame)));
}
}

TEST_CASE("sysmix output purge after detach preserves wrapped audio and notifications",
          "[sysmix][output_purge]") {
    Context context;
    Cursor(context, 2048, 2);
    Sample(context, 2048, 1.0f, -1.0f);
    Sample(context, 0, 0.25f, -0.25f);
    Sample(context, 1, 0.5f, -0.5f);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetDataNotification(2, 2046).IsSuccess());
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    REQUIRE(stream.SetEngaged(false).IsSuccess());

    u32 purged = 99;
    REQUIRE(stream.Purge(1, purged).IsSuccess());
    REQUIRE(purged == 1);
    REQUIRE(ReadCursor(context) == 0);
    u32 write{};
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x1004, write));
    REQUIRE(write == 2);
    REQUIRE(stream.GetState() == 1);
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    stream.Pump();
    REQUIRE(ReadCursor(context) == 0);
    REQUIRE(stream.GetCounters().accepted_frames == 0);

    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 2);
    REQUIRE(stream.GetCounters().accepted_frames == 2);
    REQUIRE(stream.GetCounters().faults == 0);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    REQUIRE(output[0] == 8192);
    REQUIRE(output[1] == -8192);
    REQUIRE(output[2] == 16384);
    REQUIRE(output[3] == -16384);
    REQUIRE(std::ranges::all_of(std::span{output}.subspan(4), [](s16 value) { return value == 0; }));
}

TEST_CASE("sysmix output purge clamps to available frames and accepts an empty ring",
          "[sysmix][output_purge]") {
    Context context;
    Cursor(context, 2047, 1);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    u32 requested{};
    SECTION("zero discards the buffered ring") { requested = 0; }
    SECTION("large request is bounded by occupancy") { requested = 4096; }
    u32 purged = 99;
    REQUIRE(stream.Purge(requested, purged).IsSuccess());
    REQUIRE(purged == 3);
    REQUIRE(ReadCursor(context) == 1);
    REQUIRE(stream.Purge(requested, purged).IsSuccess());
    REQUIRE(purged == 0);
    REQUIRE(stream.GetState() == 1);
    REQUIRE(stream.GetCounters().faults == 0);
    u32 write{};
    REQUIRE(context.Memory().Read32Acquire(Context::Base + 0x1004, write));
    REQUIRE(write == 1);
}

TEST_CASE("sysmix output purge preserves admitted audio and accepts fresh frames while engaged",
          "[sysmix][output_purge]") {
    Context context;
    Cursor(context, 0, 480);
    for (u32 i = 0; i < 480; ++i) {
        Sample(context, i, i < 240 ? 0.25f : 1.0f, i < 240 ? -0.25f : -1.0f);
    }
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 240);
    REQUIRE(context.sink.last->GetQueueSize() == 1);
    u32 purged = 99;
    REQUIRE(stream.Purge(0, purged).IsSuccess());
    REQUIRE(purged == 240);
    REQUIRE(ReadCursor(context) == 480);
    REQUIRE(stream.GetState() == 2);
    REQUIRE(context.sink.last->GetQueueSize() == 1);
    REQUIRE(stream.GetCounters().accepted_frames == 240);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    for (u32 i = 0; i < output.size(); ++i) {
        REQUIRE(output[i] == (i % 2 == 0 ? 8192 : -8192));
    }
    REQUIRE_FALSE(context.sink.last->HasPendingAudio());

    for (u32 i = 480; i < 720; ++i) {
        Sample(context, i, 0.5f, -0.5f);
    }
    const u32 write = 720;
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1004, &write, sizeof(write)));
    stream.Pump();
    REQUIRE(ReadCursor(context) == 720);
    REQUIRE(stream.GetCounters().accepted_frames == 480);
    REQUIRE(stream.GetCounters().faults == 0);
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    for (u32 i = 0; i < output.size(); ++i) {
        REQUIRE(output[i] == (i % 2 == 0 ? 16384 : -16384));
    }
}

TEST_CASE("sysmix output purge rejects invalid stream state and cursor ownership",
          "[sysmix][output_purge]") {
    Context context;
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    u32 purged = 99;
    REQUIRE(stream.Purge(0, purged) == Service::Audio::ResultOperationFailed);
    REQUIRE(purged == 0);
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    SECTION("guest changes the server-owned read cursor") { Cursor(context, 1, 2); }
    SECTION("guest publishes an out-of-range write cursor") { Cursor(context, 0, 2049); }
    SECTION("read cursor is unmapped") { context.Unmap(Context::Base); }
    SECTION("write cursor is unmapped") { context.Unmap(Context::Base + 0x1000); }
    REQUIRE(stream.Purge(0, purged) == Service::Audio::ResultInvalidAddressInfo);
    REQUIRE(purged == 0);
    REQUIRE(stream.GetState() == 0);
    REQUIRE(stream.GetCounters().faults == 1);
    REQUIRE(stream.Purge(0, purged) == Service::Audio::ResultOperationFailed);
    REQUIRE(purged == 0);
}

TEST_CASE("sysmix validates the exact F32 ring allocation and rejects unsupported formats", "[sysmix]") {
    Context context;
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(3, 2, 48000, 2048, 0, 0).IsFailure());
    REQUIRE(stream.Open(5, 1, 48000, 2048, 0, 0).IsFailure());
    REQUIRE(stream.Open(5, 2, 44100, 2048, 0, 0).IsFailure());
    REQUIRE(stream.Open(5, 2, 48000, 2048, 1, 0).IsFailure());
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 1).IsFailure());
    REQUIRE(stream.Open(5, 2, 48000, 0, 0, 0).IsFailure());
    REQUIRE(stream.Open(5, 2, 48000, 0xffffffff, 0, 0).IsFailure());
    REQUIRE(context.sink.last == nullptr);
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsFailure());
    REQUIRE(context.process->GetReferenceCount() == 2);
    REQUIRE(SysmixOutput::ValidateRegion(*context.process, Context::Base + 1,
                                         Context::Size, 0x24).IsFailure());
    REQUIRE(SysmixOutput::ValidateRegion(*context.process, Context::Base,
                                         Context::Size, 0x84).IsFailure());
    SysmixOutput short_stream{context.system, *context.process, context.sink, 2, Context::Base,
                              0x5000, 0x24};
    REQUIRE(short_stream.Open(5, 2, 48000, 2048, 0, 0).IsFailure());
}

TEST_CASE("sysmix copies the physical extra slot and wrap then publishes consumer", "[sysmix]") {
    Context context;
    Cursor(context, 2048, 1);
    Sample(context, 2048, 0.5f, -0.5f);
    Sample(context, 0, 1.0f, -1.0f);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 1);
    REQUIRE(stream.GetCounters().accepted_frames == 2);
    REQUIRE(stream.GetCounters().nonzero_frames == 2);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    REQUIRE(output[0] == 16384);
    REQUIRE(output[1] == -16384);
    REQUIRE(output[2] == 32767);
    REQUIRE(output[3] == -32768);
    REQUIRE(std::ranges::all_of(std::span{output}.subspan(4), [](s16 value) { return value == 0; }));
    REQUIRE(context.backing[0x5048] == 0);
    REQUIRE(context.backing[0x5fff] == 0);
}

TEST_CASE("sysmix plays the Sports Resort Q15 movie ring including wrap and gain", "[sysmix]") {
    Context context;
    Cursor(context, 960, 1);
    const std::array<s16, 2> last{16384, -16384};
    const std::array<s16, 2> first{32767, -32768};
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040 + 960 * 4,
                                        last.data(), sizeof(last)));
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040, first.data(), sizeof(first)));
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        0x3000, 0x24};
    REQUIRE(stream.Open(2, 2, 48000, 960, 0, 0).IsSuccess());
    REQUIRE(stream.SetGain(0.5f, 0).IsSuccess());
    REQUIRE(stream.SetDataNotification(2, 480).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 1);
    REQUIRE(stream.GetCounters().accepted_frames == 2);
    REQUIRE(stream.GetCounters().nonzero_frames == 2);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    REQUIRE(output[0] == 8192);
    REQUIRE(output[1] == -8192);
    REQUIRE(output[2] == 16383);
    REQUIRE(output[3] == -16384);
    REQUIRE(std::ranges::all_of(std::span{output}.subspan(4), [](s16 value) { return value == 0; }));
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
}

TEST_CASE("sysmix advertises implemented stereo and surround PCM formats", "[sysmix]") {
    REQUIRE(SysmixOutput::IsFormatSupported(2, 2, 48000));
    REQUIRE(SysmixOutput::IsFormatSupported(5, 2, 48000));
    for (const u32 format : {0U, 1U, 3U, 4U, 6U, 258U, 261U}) {
        REQUIRE_FALSE(SysmixOutput::IsFormatSupported(format, 2, 48000));
    }
    REQUIRE_FALSE(SysmixOutput::IsFormatSupported(2, 1, 48000));
    REQUIRE(SysmixOutput::IsFormatSupported(2, 6, 48000));
    REQUIRE(SysmixOutput::IsFormatSupported(5, 6, 48000));
    REQUIRE_FALSE(SysmixOutput::IsFormatSupported(5, 8, 48000));
    REQUIRE_FALSE(SysmixOutput::IsInputFormatSupported(5, 6, 48000));
    REQUIRE_FALSE(SysmixOutput::IsFormatSupported(2, 2, 44100));
}

TEST_CASE("sysmix mode2 event uses actual free frames even before engagement", "[sysmix]") {
    Context context;
    Cursor(context, 0, 600);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetDataNotification(1, 1536).IsFailure());
    REQUIRE(stream.SetDataNotification(2, -1).IsFailure());
    REQUIRE(stream.SetDataNotification(2, 2049).IsFailure());
    REQUIRE(stream.SetDataNotification(2, 1536).IsSuccess());
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    stream.Pump();
    REQUIRE(ReadCursor(context) == 0);
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 240);
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.SetEngaged(false).IsSuccess());
    REQUIRE(stream.GetDataEvent()->Clear(context.system.Kernel()).IsSuccess());
    stream.Pump();
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.SetDataNotification(0, 0).IsSuccess());
    REQUIRE(stream.GetDataEvent()->Clear(context.system.Kernel()).IsSuccess());
    stream.Pump();
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
}

TEST_CASE("sysmix backpressure leaves guest ownership unchanged and resumes in order", "[sysmix]") {
    Context context;
    Cursor(context, 0, 2048);
    for (u32 i = 0; i < 2048; ++i) {
        Sample(context, i, 0.25f, -0.25f);
    }
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    for (u32 i = 0; i < 3; ++i) {
        stream.Pump();
    }
    REQUIRE(ReadCursor(context) == 720);
    stream.Pump();
    REQUIRE(ReadCursor(context) == 720);
    REQUIRE(stream.GetCounters().accepted_frames == 720);
    REQUIRE(stream.GetCounters().backpressure == 1);
    REQUIRE(stream.GetCounters().copied_frames == 960);
    REQUIRE(stream.GetCounters().retried_frames == 240);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    stream.Pump();
    REQUIRE(ReadCursor(context) == 960);
    REQUIRE(context.sink.last->GetQueueSize() == 3);
}

TEST_CASE("sysmix pads a short guest block with silence for the host", "[sysmix]") {
    Context context;
    Cursor(context, 0, 100);
    for (u32 i = 0; i < 100; ++i) {
        Sample(context, i, 0.25f, -0.25f);
    }
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 100);
    REQUIRE(stream.GetCounters().accepted_frames == 100);
    REQUIRE(stream.GetCounters().padded_frames == 140);
    REQUIRE(stream.GetCounters().admitted_blocks == 1);
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    for (u32 i = 0; i < 200; ++i) {
        REQUIRE(output[i] == (i % 2 == 0 ? 8192 : -8192));
    }
    REQUIRE(std::ranges::all_of(std::span{output}.subspan(200),
                                [](s16 value) { return value == 0; }));
}

TEST_CASE("sysmix waits for a whole block while the host still has audio queued", "[sysmix]") {
    Context context;
    Cursor(context, 0, 340);
    for (u32 i = 0; i < 340; ++i) {
        Sample(context, i, 0.25f, -0.25f);
    }
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 240);
    REQUIRE(context.sink.last->GetQueueSize() == 1);

    // The host starts that block and plays 100 of its frames: nothing is queued any more, but
    // 140 frames are still to play.
    std::array<s16, 960> output{};
    context.sink.last->ProcessAudioOutAndRender(std::span{output}.first(200), 100);
    REQUIRE(context.sink.last->GetQueueSize() == 0);
    REQUIRE(context.sink.last->HasPendingAudio());

    // 100 frames are left in the ring: no zero-padded block while the host has audio to play.
    stream.Pump();
    REQUIRE(ReadCursor(context) == 240);
    REQUIRE(stream.GetCounters().deferred == 1);
    REQUIRE(stream.GetCounters().admitted_blocks == 1);

    // The guest tops the ring up: the next block plays straight on, with no silence between.
    Cursor(context, 240, 480);
    for (u32 i = 340; i < 480; ++i) {
        Sample(context, i, 0.25f, -0.25f);
    }
    stream.Pump();
    REQUIRE(ReadCursor(context) == 480);
    REQUIRE(stream.GetCounters().accepted_frames == 480);
    context.sink.last->ProcessAudioOutAndRender(std::span{output}.subspan(200), 380);
    REQUIRE(std::ranges::all_of(output, [](s16 value) { return value == 8192 || value == -8192; }));
    REQUIRE_FALSE(context.sink.last->HasPendingAudio());

    // Once the host has drained, a short block is a real underrun and is sent padded, so a guest
    // waiting on more free space than those frames leave (threshold 2040) is woken.
    REQUIRE(context.sink.last->GetQueueSize() == 0);
    Cursor(context, 480, 500);
    REQUIRE(stream.SetDataNotification(2, 2040).IsSuccess());
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    stream.Pump();
    REQUIRE(ReadCursor(context) == 500);
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.GetCounters().accepted_frames == 500);
    REQUIRE(stream.GetCounters().deferred == 1);
}

TEST_CASE("sysmix detects unmapped cursor publication even though exclusive store reports success", "[sysmix]") {
    Context context;
    Cursor(context, 0, 240);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    context.sink.last->on_admit = [&] { context.Unmap(Context::Base); };
    stream.Pump();
    REQUIRE(context.Memory().WriteExclusive32(Context::Base + 0x44, 240, 0));
    u32 read{};
    REQUIRE_FALSE(context.Memory().Read32Acquire(Context::Base + 0x44, read));
    REQUIRE(stream.GetCounters().accepted_frames == 0);
    REQUIRE(stream.GetCounters().faults == 1);
    REQUIRE(stream.GetCounters().admitted_blocks == 1);
    REQUIRE(stream.GetState() == 0);
    std::array<s16, 480> output{};
    output.fill(99);
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    REQUIRE(std::ranges::all_of(output, [](s16 value) { return value == 0; }));
    stream.Pump();
    REQUIRE(stream.GetCounters().faults == 1);
}

TEST_CASE("sysmix rejects unmapped payload and tampered cursors without admission", "[sysmix]") {
    Context context;
    Cursor(context, 2048, 1);
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    SECTION("missing payload page") {
        context.Unmap(Context::Base + 0x5000);
    }
    SECTION("producer outside physical modulus") {
        Cursor(context, 2048, 2049);
    }
    SECTION("consumer changed by guest") {
        Cursor(context, 0, 1);
    }
    stream.Pump();
    REQUIRE(stream.GetCounters().faults == 1);
    REQUIRE(stream.GetCounters().accepted_frames == 0);
    REQUIRE(context.sink.last->GetQueueSize() == 0);
}

TEST_CASE("sysmix close releases host stream and allows reopening without stale samples", "[sysmix]") {
    Context context;
    Cursor(context, 0, 1);
    Sample(context, 0, 0.5f, 0.5f);
    {
        SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                            Context::Size, 0x24};
        REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
        REQUIRE(stream.SetEngaged(true).IsSuccess());
        stream.Pump();
        REQUIRE(stream.SetEngaged(false).IsSuccess());
        REQUIRE(context.sink.last->GetQueueSize() == 1);
        stream.Close();
        REQUIRE(context.sink.last == nullptr);
        const auto ticks = stream.GetCounters().ticks;
        stream.Pump();
        REQUIRE(stream.GetCounters().ticks == ticks);
        Cursor(context, 0, 0);
        REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
        REQUIRE(context.sink.last->GetQueueSize() == 0);
    }
    REQUIRE(context.sink.closed == 2);
    REQUIRE(context.process->GetReferenceCount() == 1);
}

TEST_CASE("sysmix requires initialized and successfully started host output", "[sysmix]") {
    Context context;
    context.sink.ready = false;
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsFailure());
    REQUIRE(context.sink.last == nullptr);
    context.sink.ready = true;
    context.sink.fail_start = true;
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsFailure());
    stream.Pump();
    REQUIRE(stream.GetCounters().admitted_blocks == 0);
}

TEST_CASE("sysmix conversion handles gain clipping and nonfinite values", "[sysmix]") {
    Context context;
    Cursor(context, 0, 2);
    Sample(context, 0, 2.0f, -2.0f);
    Sample(context, 1, std::numeric_limits<f32>::quiet_NaN(),
           std::numeric_limits<f32>::infinity());
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetGain(0.25f, 1).IsFailure());
    REQUIRE(stream.SetGain(0.25f, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    std::array<s16, 480> output{};
    context.sink.last->ProcessAudioOutAndRender(output, 240);
    REQUIRE(output[0] == 16384);
    REQUIRE(output[1] == -16384);
    REQUIRE(output[2] == 0);
    REQUIRE(output[3] == 0);
}

TEST_CASE("sysmix child lifetime retains registry after user session closes", "[sysmix]") {
    Context context;
    using namespace Service::Audio;
    auto registry = std::make_shared<Nx2SysmixStreams>(context.system);
    std::weak_ptr<Nx2SysmixStreams> weak = registry;
    auto output = std::make_shared<SysmixOutput>(context.system, *context.process, context.sink,
                                                1, Context::Base, Context::Size, 0x24);
    registry->streams.emplace(1, output);
    auto child = std::make_shared<INx2SysmixStreamSession>(context.system, registry, output, 1);
    auto user = std::make_shared<INx2SysmixUserSession>(context.system, registry, &*context.process);
    registry.reset();
    user.reset();
    REQUIRE_FALSE(weak.expired());
    REQUIRE(output->Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(weak.lock()->SetEngaged(1, true, nullptr).IsFailure());
    REQUIRE(weak.lock()->SetEngaged(1, true, &*context.process).IsSuccess());
    context.system.CoreTiming().cpu_ticks += Common::WallClock::CPUTickFreq / 200;
    context.system.CoreTiming().Advance();
    REQUIRE(output->GetCounters().admitted_blocks == 1);
    // Drain the silent block, so the empty ring underruns again instead of waiting on the host.
    std::array<s16, 480> drained{};
    context.sink.last->ProcessAudioOutAndRender(drained, 240);
    context.system.CoreTiming().cpu_ticks += Common::WallClock::CPUTickFreq / 10;
    context.system.CoreTiming().Advance();
    REQUIRE(output->GetCounters().ticks == 2);
    REQUIRE(output->GetCounters().admitted_blocks == 2);
    child.reset();
    REQUIRE(weak.expired());
    REQUIRE(output->GetState() == 0);
    REQUIRE(context.sink.last == nullptr);
    context.system.CoreTiming().cpu_ticks += Common::WallClock::CPUTickFreq / 200;
    context.system.CoreTiming().Advance();
    REQUIRE(output->GetCounters().ticks == 2);
}

TEST_CASE("sysmix does not report successful disengage after backend stop failure", "[sysmix]") {
    Context context;
    SysmixOutput stream{context.system, *context.process, context.sink, 1, Context::Base,
                        Context::Size, 0x24};
    REQUIRE(stream.Open(5, 2, 48000, 2048, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    context.sink.last->fail_stop = true;
    REQUIRE(stream.SetEngaged(false).IsFailure());
    REQUIRE(stream.GetState() == 0);
    REQUIRE(stream.GetCounters().faults == 1);
    stream.Pump();
    REQUIRE(stream.GetCounters().admitted_blocks == 0);
}

TEST_CASE("sysmix six-channel float ring preserves wrapped frames and pads all channels",
          "[sysmix][surround]") {
    const u32 creation_flags = GENERATE(4U, 0x24U);
    Context context;
    context.sink.host_channels = 6;
    constexpr u32 Slots = 512;
    Cursor(context, Slots - 1, 1);
    const std::array<f32, 6> first{0.125f, -0.125f, 0.25f, -0.25f, 2.0f, -2.0f};
    const std::array<f32, 6> second{std::numeric_limits<f32>::quiet_NaN(),
        std::numeric_limits<f32>::infinity(), 0.5f, -0.5f, 0.75f, -0.75f};
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040 + (Slots - 1) * 24,
                                       first.data(), sizeof(first)));
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040, second.data(), sizeof(second)));
    SysmixOutput stream{context.system, *context.process, context.sink, 1,
                        Context::Base, Context::Size, creation_flags};
    REQUIRE(stream.Open(5, 6, 48000, Slots - 1, 0, 0).IsSuccess());
    REQUIRE(stream.SetGain(0.5f, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 1);
    std::array<s16, 240 * 6 + 2> output;
    output.fill(12345);
    context.sink.last->ProcessAudioOutAndRender(std::span{output}.subspan(1, 240 * 6), 240);
    REQUIRE(output.front() == 12345);
    REQUIRE(output.back() == 12345);
    const std::array<s16, 12> expected{2048, -2048, 4096, -4096, 32767, -32768,
                                     0, 0, 8192, -8192, 12288, -12288};
    for (size_t i = 0; i < expected.size(); ++i) CHECK(output[i + 1] == expected[i]);
    CHECK(std::ranges::all_of(std::span{output}.subspan(13, 240 * 6 - 12),
                             [](s16 value) { return value == 0; }));
}

TEST_CASE("sysmix six-channel PCM admission keeps frame cursors under backpressure",
          "[sysmix][surround]") {
    const u32 creation_flags = GENERATE(4U, 0x24U);
    Context context;
    context.sink.host_channels = 6;
    constexpr u32 Frames = 240 * 4;
    Cursor(context, 0, Frames);
    std::array<s16, Frames * 6> input;
    for (size_t frame = 0; frame < Frames; ++frame) {
        for (size_t channel = 0; channel < 6; ++channel)
            input[frame * 6 + channel] = static_cast<s16>(frame + channel * 1000);
    }
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040, input.data(), sizeof(input)));
    SysmixOutput stream{context.system, *context.process, context.sink, 1,
                        Context::Base, Context::Size, creation_flags};
    REQUIRE(stream.Open(2, 6, 48000, 1023, 0, 0).IsSuccess());
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    for (int i = 0; i < 3; ++i) stream.Pump();
    REQUIRE(ReadCursor(context) == 720);
    stream.Pump();
    REQUIRE(ReadCursor(context) == 720);
    std::array<s16, 240 * 6> output;
    for (u32 block = 0; block < 4; ++block) {
        context.sink.last->ProcessAudioOutAndRender(output, 240);
        for (size_t sample = 0; sample < output.size(); ++sample)
            CHECK(output[sample] == input[block * output.size() + sample]);
        if (block == 0) stream.Pump();
    }
    REQUIRE(ReadCursor(context) == Frames);
}

TEST_CASE("sysmix creation flags retain exact direction and memory validation", "[sysmix][output_flags]") {
    Context context;
    for (u32 flags : {4U, 0x24U}) {
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base, Context::Size, flags)
                  .IsSuccess());
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base + 1, Context::Size, flags)
              == Service::Audio::ResultInvalidAddressInfo);
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base, 0x1000, flags)
              == Service::Audio::ResultInvalidAddressInfo);
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base + Context::Size,
                                          Context::Size, flags)
              == Service::Audio::ResultInvalidAddressInfo);
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base, Context::Size, flags, true)
              == Service::Audio::ResultNotSupported);
    }
    for (u32 flags : {0U, 1U, 2U, 3U, 5U, 6U, 0x20U, 0x44U, 0x64U, 0x84U, 0xa4U, 0xffffffffU}) {
        CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base, Context::Size, flags)
              == Service::Audio::ResultNotSupported);
    }
    CHECK(SysmixOutput::ValidateRegion(*context.process, Context::Base, Context::Size, 2, true)
              .IsSuccess());
    CHECK(context.sink.last == nullptr);
}

TEST_CASE("sysmix six-channel output modes preserve admitted audio through detached purge",
          "[sysmix][surround][output_flags][output_purge]") {
    const u32 creation_flags = GENERATE(4U, 0x24U);
    Context context;
    context.sink.host_channels = 6;
    constexpr u32 Slots = 512;
    constexpr u32 Start = Slots - 1;
    auto write_frames = [&](u32 start, u32 count, s16 base) {
        for (u32 frame = 0; frame < count; ++frame) {
            std::array<s16, 6> samples;
            for (u32 channel = 0; channel < samples.size(); ++channel)
                samples[channel] = static_cast<s16>(base + frame + channel * 1000);
            REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1040 +
                        u64{(start + frame) % Slots} * sizeof(samples), samples.data(), sizeof(samples)));
        }
    };
    Cursor(context, Start, 479);
    write_frames(Start, 480, 100);
    SysmixOutput stream{context.system, *context.process, context.sink, 1,
                        Context::Base, Context::Size, creation_flags};
    REQUIRE(stream.Open(2, 6, 48000, Slots - 1, 0, 0).IsSuccess());
    REQUIRE(stream.SetDataNotification(2, Slots - 1).IsSuccess());
    REQUIRE_FALSE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == 239);
    REQUIRE(context.sink.last->GetQueueSize() == 1);
    REQUIRE(stream.SetEngaged(false).IsSuccess());
    u32 purged{};
    REQUIRE(stream.Purge(0, purged).IsSuccess());
    REQUIRE(purged == 240);
    REQUIRE(ReadCursor(context) == 479);
    REQUIRE(context.sink.last->GetQueueSize() == 1);
    REQUIRE(stream.GetDataEvent()->IsSignaled(context.system.Kernel()));
    stream.Pump();
    REQUIRE(stream.GetCounters().accepted_frames == 240);
    write_frames(479, 240, 800);
    const u32 write = 207;
    REQUIRE(context.Memory().WriteBlock(Context::Base + 0x1004, &write, sizeof(write)));
    REQUIRE(stream.SetEngaged(true).IsSuccess());
    stream.Pump();
    REQUIRE(ReadCursor(context) == write);
    REQUIRE(stream.GetCounters().accepted_frames == 480);
    REQUIRE(stream.GetCounters().faults == 0);
    std::array<s16, 240 * 6> output;
    for (s16 base : {100, 800}) {
        context.sink.last->ProcessAudioOutAndRender(output, 240);
        for (u32 frame = 0; frame < 240; ++frame)
            for (u32 channel = 0; channel < 6; ++channel)
                CHECK(output[frame * 6 + channel] == base + frame + channel * 1000);
    }
}
