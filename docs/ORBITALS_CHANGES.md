# Orbitals change notes

## Build 1 candidate

The numbered release is being prepared. This evidence describes the accepted
source candidate; the final numbered executable still requires its own checks.

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

The latest candidate passed **111 targeted cases and 51,826,471 assertions**.
The output-flag regression failed in the baseline and passed after correction.

On an Intel Core i7-12700F / NVIDIA RTX 3080 system, the opening played naturally
at **30.025 and 30.003 FPS** in measured cutscene windows. The fixed two-player
room measured **24.332 and 24.066 FPS**. Six-channel output attached and remained
healthy; one measured room interval had approximately 14 ms of padding. Across
the full audio counter interval, padding totaled approximately 423 ms over
770 seconds. Geometry appeared intact during the recorded checks and shutdown
completed. A subsequent user report describes left-view-only jittering objects,
glare and static lines in the waiting room, with the right view unaffected.
That visual-correctness issue is unresolved and has not been causally isolated.
The run used 1x resolution and the
accepted PAUSE selection. These results do not isolate the latest audio fix's
performance effect.

Earlier controlled comparisons supported the unlock change and PAUSE selection.
Cache-line alignment and half-resolution candidates did not improve the room
and were not selected. Different exterior views reached 30 FPS, which is not
equivalent to the heavier initial room.

Stable 30 FPS throughout, later chapters and every output device remain
unverified. Brief loading/streaming stalls and audio underfills can occur.
Known startup diagnostics include three disk-cache compute replay failures and
a source-depth assertion burst. Those messages alone do not establish skipped
live workloads or missing geometry in the current run.

Future source changes receive new commits. Executable releases use new build
numbers; latest and recommended remain separate with their validation rationale.
