# Orbitals change notes

## Unreleased viewport dependency correction

The development source now marks Vulkan viewport state dirty when `depth_mode`
changes. That register affects `minDepth`, so a mode-only update must refresh the
existing dynamic viewport even when transform and rectangle registers stay the
same. The correction adds one dependency-table entry; it does not change shader
translation, guest synchronization or the viewport calculation itself.

Four tests call the complete production table builder and actual state-tracker
consumption paths. With only the test seam added, the missing depth dependency
failed while the other three cases passed (342 of 343 assertions passed). Adding
only the mapping made all four cases and 343 assertions pass. The combined
regression run passed 115 cases and 51,826,450 assertions. These are source-test
results, not validation of a future numbered binary.

The optional viewport observer remains off by default. A baseline observation
through menus, opening cutscene, co-op room, camera rotation and movement logged
no same-channel depth-mode changes or stale-depth mismatches. That run did not
exercise this defect and does not connect the correction to the reported
left-view jitter, glare or static lines. No FPS improvement is claimed.

Build 1 remains the latest recommended numbered baseline until a separately
identified candidate passes its own runtime checks. The release and its assets
are unchanged.

## Build 1

[Build 1](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1) was
published as a pre-release on 2026-11-09. It is the latest and recommended numbered
baseline. Its source remains pinned to
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
