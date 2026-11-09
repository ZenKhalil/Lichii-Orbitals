# Optional Vulkan viewport dependency observer

This observer investigates the depth-mode invalidation defect in
[issue 3](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3). It does not fix
the dirty table or change emitted Vulkan commands. Its connection to the
reported left-view artifacts remains unconfirmed. Build 1 does not include
this observer and remains the recommended numbered baseline.

Set `EDEN_NX2_VIEWPORT_DEPENDENCY_TRACE=1` before starting the diagnostic build.
Only that exact value enables it; unset and other values keep it off. Restart
after changing the variable. Keep other heavy diagnostics disabled when
collecting a representative observation.

The observer compares the requested viewport depth ranges with the last ranges
recorded for Vulkan when a depth-mode change leaves viewport state clean. It
does not mark state dirty or replace any computed values. It compares at most
the device's supported viewport count, bounded by the existing sixteen slots.

Summary logs appear at most every five seconds during `TickFrame`. Counters are
cumulative: `same_channel_mode_changes`, `clean_mode_changes`,
`stale_depth_mode_draws`, and `depth_mismatch_draws` have different meanings and
must be interpreted together. The first 32 distinct frame/channel/mode mismatch
records are retained, along with the latest mismatch. Depth values are logged
as their float bit patterns. `frame` is this observer's guarded `TickFrame`
count, not a Present ID or the other frame-trace counter.

A bounded atomic try-guard protects diagnostic state only. Contention,
reentrancy or a different owner permanently disables state observation for
that renderer and emits `coverage_unknown=true` once. It never waits or adds
an emulation lock. The warning invalidates coverage and causal conclusions for
the affected interval, including a record already in flight when ownership
became unknown. Such a run cannot support a zero-mismatch conclusion. Even
a complete run with no detected mismatch does not establish visual correctness
or exclude other state defects. Enabling observation has some measurement cost.

## Focused tests on Windows

In an x64 MSVC developer environment, with Python and clang-cl installed:

```powershell
. ./tools/windows/load-msvc-env.ps1
python tools/tests/run_viewport_dependency_trace.py `
    --compiler clang-cl --build-dir build/viewport-observer-tests
```

The test includes the actual production header, without a copied implementation.
It runs 157 deterministic checks in each of nine fresh environment processes,
covering strict cached opt-in, ownership/unknown handling, exception cleanup,
viewport bounds, channel changes, dirty/emission behavior and reporting limits.
The runner retains compiler output, per-case logs and input hashes in a unique
subdirectory. These helper tests do not validate Vulkan pixels or establish the
cause of a gameplay artifact.
