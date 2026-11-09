# Exclusive-monitor two-PAUSE experiment

This is an unshipped experiment based on Candidate 5 source commit
`59f6372b5958e52ffce224567baac6777c07a759`. It is off by default. The full
application-linked tests and gameplay comparison have **not run**. There is no
claim of improved FPS, fairness, or resolution of the reported rendering artifact.

## Candidate behavior

The current contended lock loop executes one `PAUSE`, reads the held-lock word,
and retries the atomic acquisition only after observing it free. The candidate
executes two `PAUSE` instructions between those reads. It does not introduce a
counter register, another memory access, an OS sleep, or an unbounded backoff.

Only the exclusive-monitor JIT caller can request the candidate. Ordinary C++
spinlock callers retain the default one-PAUSE code. The WAITPKG path is unchanged,
including its monitor-before-recheck protocol. Atomic `XCHG` acquisition and
release, guest reservation bookkeeping, ordered memory access and guest barriers
remain intact. This does not enable ignore-monitor or relax CPU accuracy.

`EDEN_NX2_EXCLUSIVE_PAUSE2=1` selects the candidate at startup; only the exact string
`1` enables it. The result is cached, so a restart is required. This switch does
not disable WAITPKG. The intended comparison also retains the existing
`EDEN_NX2_DISABLE_WAITPKG=1` selection. When WAITPKG is selected, either PAUSE2 value emits the unchanged WAITPKG
path. On a host without WAITPKG support, the PAUSE path is already selected;
exact PAUSE2 `1` requests two pauses even if DISABLE_WAITPKG is unset. All other
PAUSE2 values retain one pause on that path. No launcher, package switch file,
user configuration or numbered-release default has been changed here.

More pauses could reduce repeated lock reads, but they could also increase
handoff latency. Neither version guarantees fairness. Previously sampled lock
waiting across several guest threads is not a recoverable frame-time budget.

## Completed isolated checks

An immutable standalone snapshot of the candidate passed 194 actual-emitter
checks: deterministic held-lock/retry control flow, five pointer-register cases,
six-thread exclusion and progress, relaxed-atomic payload handoff, C++ cleanup,
and a controlled leaf exception check. An independent byte fixture passed 55
checks in each of 18 fresh environment processes. These are repeated checks
across switch combinations, not 990 independent tests.

The fixtures linked no emulator libraries. Test reporting macros were adapted
for the standalone executable; the emitted lock bodies were the actual candidate
bodies. A deliberately stalled child exercised the retained-child timeout and
termination safeguard. The short contention runs are correctness observations,
not performance benchmarks. The controlled exception is not a production JIT
fastmem-fault recovery test.

## Application-linked tests still required

`src/tests/common/dynarmic_pause2.cpp` adds four cases beside the existing
Windows x64 Dynarmic lock tests. The first three cover generated lock control
flow, native contention, payload publication and cleanup. The fourth runs two
real A64 JITs with one shared monitor and requires:

- A same-value competing `STXR` to invalidate the first processor's reservation.
- A fresh load and an unrelated-address reservation to retain valid success paths.
- `ClearProcessor` and guest `CLREX` to invalidate the appropriate reservation.
- Both callback and aligned writable fastmem paths to behave correctly, with zero
  data callbacks required for the inline-fastmem case.

The JIT fixture uses `BRK` to halt between instructions. `SVC` cannot delimit this
test because the current supervisor-call emitter clears local exclusive state.
These are new, **unrun** 64-bit tests with sequentially interleaved JITs. They do
not establish simultaneous-JIT execution, 128-bit operation coverage, or full
Core/GPU memory-coherency behavior. Existing shipped lock tests contain no
same-value-STXR case; their prior results are not being relabeled as this test.

After review and an explicit no-game build window:

1. Rebuild all emitter consumers and link the actual application test target.
   The new emitter parameter changes its C++ signature; hot patching is unsuitable.
2. Run the full suite with PAUSE2 unset and the accepted WAITPKG-disabled selection.
   With the separately committed issue 27 regression, the expected focused
   total is 166 cases, up from Candidate 5's 161; record the actual result. This is the selected application regression
   filter, not every test compiled into the executable. Keep the separate
   shader-compiler checks appropriate to the final integration.
3. Run `[dynarmic_pause2]` in fresh processes with `EDEN_NX2_DISABLE_WAITPKG=1`
   and `EDEN_NX2_EXCLUSIVE_PAUSE2=0`, then `1`. Require the real JIT controls above
   to pass in both processes. Keep the existing WAITPKG and old-caller byte controls.
4. Retain source, executable and output hashes, failure logs, and bounded child
   watchdogs. Do not run these native checks during gameplay measurements.

Only after those gates pass should one experimental package be compared in an
A/B/A sequence at the same scene, poses, settings and power state. Verify the
actual JIT wait-loop motifs and assess frame-time tails as well as averages.
Do not combine the comparison with further viewport, timer, accuracy or monitor
layout changes. Keep the default if a benefit is not repeatable.

## Provenance and scope

The local cached donor revision `d97b0852b546569916b98aef0993d559fdf59b63`
and previously pinned monitor-related diffs did not contain an equivalent
two-PAUSE change. This was a bounded cached-source review, not a claim of a fresh
exhaustive upstream survey. The accepted local monitor corrections remain the
baseline; no lock-free-monitor proposal or moving upstream branch was imported.

This branch is for a separately reviewed experiment. It must not be represented
as a numbered build, a main-branch change, or a proven optimization before the
remaining tests and runtime comparison are complete.

## Candidate 5 refresh

This preparation preserves the four previously reviewed production files and the
four-case application fixture byte-for-byte from the earlier experiment. Their
production preimages are byte-identical between the earlier Build 2 base and
the Candidate 5 commit above. The only test-registration change adds this file
to the existing Windows x64 Dynarmic block; Candidate 5's other registrations
remain intact. The current README and viewport documentation are not replaced.

The isolated 194-check and 18-process results above belong to the earlier
immutable source snapshots. No compiler, linked test, GPU fixture, emulator or
benchmark was run for this refresh. Candidate 5's successful 161-case regression
does not include these four new cases. The fresh linked and same-binary A/B/A
gates remain mandatory before considering deployment. This preparation does not
change upstream import policy, production monitor behavior, or Candidate 5.

## Candidate 6 preparation

This isolated branch combines the experiment with the separately documented
[compute serialization guard](../COMPUTE_CACHE_CONFLICTS.md). Neither change has
received application-linked validation in this preparation. The first native
gate remains baseline RED then guarded GREEN for issue 27, followed by the new
PAUSE2 linked cases in fresh processes with the switch disabled and enabled.
Any future tuple-policy or trace-filter changes require their own reviewed
integration and adjustment of expected test counts.

The later performance comparison must use the same final binary with only the
PAUSE2 environment switch changed, followed by restoration of the disabled
setting. The serialization guard must be present in both arms. No gameplay
comparison, package, or improved frame rate is claimed by this source commit.
