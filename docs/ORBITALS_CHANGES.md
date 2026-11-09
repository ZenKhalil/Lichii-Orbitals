# Orbitals change notes

## Build 3 integration — Vulkan correctness

The integrated source corrects presentation and texture-transfer dependencies,
repeated buffer-upload ordering, dynamic vertex-stride declarations and the
Win32 surface instance. Eligible uniform/storage texel operations select
matching numeric-class variants from immutable compiler inputs while retaining
the prepared view format, offset and range. Known-conflicting compute captures
are rejected before cache serialization; existing cache records are preserved.

The focused recorder tests and candidate Vulkan runs are recorded in the
[validation findings](nx2/vulkan-validation-findings.md). Candidate 7 passed
174 selected native cases and 96 compiler/cache cases, then completed the full
opening, co-op movement and normal shutdown with both experiments off.
Its opening samples measured 29.59 / 29.45 FPS and stationary room samples
24.28 / 25.00 FPS. See [runtime results](C7_RUNTIME_RESULTS.md) for scope and
audio totals. These are candidate results; the numbered artifact requires its
own identity, linked tests and runtime smoke before release.

The extra-PAUSE experiment remains disabled because it showed no benefit.
The two-uniform experiment also remains disabled: its enabled Vulkan run
selected compatible types for the eligible target but hung after close.
The shutdown cause is unconfirmed and tracked in
[#34](https://github.com/ZenKhalil/Lichii-Orbitals/issues/34).
Remaining multi-resource numeric mismatches, intermittent visual artifacts and
sustained 30-FPS room performance are unresolved. Full shader preload remains 0.

## Build 2 — viewport dependency correction

Build 2 marks Vulkan viewport state dirty when `depth_mode`
changes. That register affects `minDepth`, so a mode-only update must refresh the
existing dynamic viewport even when transform and rectangle registers stay the
same. The correction adds one dependency-table entry; it does not change shader
translation, guest synchronization or the viewport calculation itself.

Four tests call the complete production table builder and actual state-tracker
consumption paths. With only the test seam added, the missing depth dependency
failed while the other three cases passed (342 of 343 assertions passed). Adding
only the mapping made all four cases and 343 assertions pass. The combined
development regression run passed 115 cases and 51,826,450 assertions. The
separately identified numbered binary passed 115 cases and 51,826,174 assertions.
Its own opening, gameplay and audio checks are recorded in
[build history](BUILD_HISTORY.md).

The optional viewport observer remains off by default. A baseline observation
through menus, opening cutscene, co-op room, camera rotation and movement logged
no same-channel depth-mode changes or stale-depth mismatches. That run did not
exercise this defect and does not connect the correction to the reported
left-view jitter, glare or static lines. No FPS improvement is claimed.

Build 2 is the latest recommended numbered release for the verified dependency
correction and retained startup/audio behavior. It measured 25.40 / 25.35 FPS in
the initial room, below the 30 FPS target. The unreleased Build 3 candidates'
[validation findings](nx2/vulkan-validation-findings.md#build-3-candidate-5)
are tracked separately. Candidate 4 retains the transfer/vertex-state repairs,
immutable compiler replay, typed storage-write variants and Win32 surface-handle
correction, then adds narrow graphics/compute uniform-fetch variants. Two compute
shaders showed compatible unsigned types at runtime; the known vertex shader
remained outside the eligibility policy. Other texel conflicts and visible glare
remain. Normal room samples measured 24.83 / 25.51 FPS, with no stable 30 FPS or
release-promotion claim. The earlier monitor audio endpoint loss and shutdown hang
also remain unresolved.

Candidate 5 adds narrow R16 compute reads and mixed-store support, plus bounded
family-exclusion diagnostics. Those diagnostics identify multiple declarations
and mixed numeric types in the remaining incompatible shaders; the narrow
families still do not cover them. Its normal run completed the opening, movement
and shutdown, measuring 26.57 / 27.17 FPS in cutscene samples and 25.05 / 24.45 FPS
in the stationary initial room. The lower cutscene result is unexplained; it
does not isolate a code regression. A left-only magenta-blue flare appeared with
both cameras viewing the same machinery from nearby positions. Build 2 remains
recommended; neither rendering correctness nor stable 30 FPS is established.

## Candidate 6 — correctness preparation and measured CPU experiment

The unreleased candidate rejects serialization when a compute capture observes
conflicting inputs. It also adds dormant ordered-uniform preparation and bounded
shader-hash filtering for diagnostics. These changes do not activate the remaining
multi-declaration game shaders. Native regression and synthetic value checks are
separate from gameplay coverage.

A same-binary off/on/off comparison confirmed the intended one/two/one PAUSE
sequences in saved generated code. The extra PAUSE was slower in both measured
window durations and remains disabled. The four accepted baseline windows ranged
from 24.17 to 25.24 FPS. One added 10.33 ms of audio padding; the other five
comparison windows added none. All processes exited without an OS kill, but the
final run logged a guest panic after the close request before cleanup completed.
The user subsequently reported that the left-view issue was not visible in the
latest check; its intermittent nature still requires further testing.
See [full results and limitations](CANDIDATE6_SAME_BINARY_RESULTS.md).

## Build 1

[Build 1](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1) was
published as a pre-release on 2026-11-09. It is the previous numbered baseline.
Its source remains pinned to
[`e28cbb16`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/e28cbb16fd4d0fc4103345b79172e8d97d1e97e6);
later documentation updates do not change that binary. See
[build history](BUILD_HISTORY.md) for the numbered executable's exact check scope.

The pinned source already contains the frame-timestamp, geometry-shader and
texture-offset changes associated with
[PR742](https://github.com/zortos293/Lichii/pull/742). These are inherited fixes.

Local changes and reviewed ports include:

- Output audio-ring purge handling and a timer deadline correction that avoids
  counting a pause twice. Purging discards requested guest-ring frames while
  preserving audio already queued for host playback. The zero-count convention
  follows the existing input-stream implementation.
- Query-prefix bounds and indexing from
  [PR718](https://github.com/zortos293/Lichii/pull/718), delayed-fence retries from
  [PR687](https://github.com/zortos293/Lichii/pull/687), DMA pusher synchronization
  from [PR609](https://github.com/zortos293/Lichii/pull/609), and native macro
  parameter handling from [PR688](https://github.com/zortos293/Lichii/pull/688).
- Query-slot lifetime and actual host completion for fence-flagged reports,
  with regressions covering the missing completion condition; render-pass
  restart corrections from [PR699](https://github.com/zortos293/Lichii/pull/699).
- Complete CPU dirty-range tracking across 2 KiB boundaries and coordinated GPU
  downloads, plus rejection-cache invalidation when captured shader-source
  pages are written. These address demonstrated defects; they do not prove
  every possible rendering fault is resolved.
- Selected output-device capabilities, stereo and six-channel output, and exact
  acceptance of output creation flags `0x4` and `0x24`. Input flags remain
  unchanged. Other unverified output flag combinations remain rejected.
- Monitor wait/retry corrections, an opt-in PAUSE waiting selector and removal
  of the redundant fence after the retained atomic XCHG unlock. Exclusion,
  guest barriers and reservation semantics are preserved.
- Bounded, default-off diagnostics for shader failures, audio negotiation,
  CPU reads, thread samples and VI2 pressure. No cooperative VI2 wait is enabled.

## Validation and remaining limits

The numbered binary passed **111 targeted cases and 51,822,609 assertions**.
The output-flag regression failed in the baseline and passed after correction.

On the Core i7-12700F / RTX 3080 test system, Build 1 measured **30.04 FPS** in
a 20-second opening cutscene window and **25.15 / 25.24 FPS** in 30- and 20-second
initial-room windows at 1× resolution. The stereo run reached co-op gameplay and
exited normally. A separate 143-second six-channel run reached the title and
Play Local menu and exited normally; it included no gameplay or FPS measurement.
Audio padding totaled approximately 396 ms in the stereo run and 57 ms in that
six-channel smoke. No host audio fault or new native shader rejection was logged.

The preceding candidate, a different executable, played the full opening with
six-channel output. Its audio counters recorded approximately 423 ms of padding
over 770 seconds, including about 14 ms in one measured room interval. That
coverage is retained as prior-candidate evidence, not attributed to the numbered
binary's shorter six-channel check.

Earlier controlled comparisons supported the unlock change and PAUSE selection.
Cache-line alignment and half-resolution candidates did not improve the room
and were not selected. Different exterior views reached 30 FPS, which is not
equivalent to the heavier initial room.

Stable 30 FPS throughout, later chapters and every output device remain
unverified. Brief loading/streaming stalls and audio underfills can occur.
Known startup diagnostics include three disk-cache compute replay failures and
a source-depth assertion burst. Those messages alone do not establish skipped
live workloads or missing geometry in the current run.

Open issues remain: [left-view artifacts (#1)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1),
[room performance (#2)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2) and
[viewport depth invalidation (#3)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3).
The viewport dependency defect is demonstrated in source but is **not fixed in
Build 1**; its relationship to the reported jitter/glare/static lines is unproven.

Future source changes receive new commits. Executable releases use new build
numbers; latest and recommended remain separate with their validation rationale.
