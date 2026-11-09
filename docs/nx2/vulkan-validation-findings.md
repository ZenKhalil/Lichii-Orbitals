# Vulkan validation findings

On 2026-11-09, a separate diagnostic run of Build 2 (`c7ada3ec4119d69fce7290e8ec6e09b847263972`)
loaded Khronos validation from commit `2f953039cb303878b3fb312f3932faeea6ade074`.
The exact loaded module and enabled core/synchronization categories were verified.
The normal Build 2 package and launcher do not enable this layer.

The run reached local co-op after skipping part of the opening, rotated the left
camera and moved both players. Validation added substantial overhead; its FPS
is not a performance result. The normal measurements in [build history](../BUILD_HISTORY.md)
remain separate.

## Recorded faults

| Finding | Recorded reports | Tracking |
|---|---:|---|
| Image clear after a layout transition without transfer-write coverage | 20 | [#6](https://github.com/ZenKhalil/Lichii-Orbitals/issues/6) |
| Depth attachment load after a transition without early-depth-read coverage | 20 | [#6](https://github.com/ZenKhalil/Lichii-Orbitals/issues/6) |
| Compute storage texel-buffer UINT declaration with signed/float views | 20 | [#7](https://github.com/ZenKhalil/Lichii-Orbitals/issues/7) |
| Vertex uniform texel-buffer FLOAT declaration with UINT view | 20 | [#7](https://github.com/ZenKhalil/Lichii-Orbitals/issues/7) |
| Indexed draw sets stride state not declared by the bound pipeline | 20 | [#8](https://github.com/ZenKhalil/Lichii-Orbitals/issues/8) |
| Non-indexed counterpart of the stride-state error | 12 | [#8](https://github.com/ZenKhalil/Lichii-Orbitals/issues/8) |

These counts are emitted reports, not total invalid operations. The configured
per-message duplicate cap was 20. Once reached, later silence cannot demonstrate
valid rendering. The raw capture reached end-of-file without truncation or I/O
failure, but that does not remove the reporting cap. Layer messages have no
individual timestamps or screen-half identity.

Source review found transfer scopes matching the clear/depth reports, missing
numeric-type handling and specialization in texel-buffer paths, and a mismatch
between vertex-state declarations and emitted stride commands. Focused fixes
must retain guest data semantics and pass fresh validation. Reinterpreting every
texel buffer as UINT merely to silence validation would be unsafe.

The layer additionally reported two null Win32 instance-handle errors, one
private-data feature error and 20 forward-compatibility warnings for 3D image
layer ranges. Ownership of the startup reports needs separate investigation.
The maintenance9 feature was disabled; that warning does not establish that
current transfers omit depth slices.

## Runtime limits

The instrumented process ran for about 11 minutes 22 seconds. No intermittent
left-view artifact was captured in the reviewed snapshots. This does not prove
absence, identify which fault caused the user's observation, or establish a fix.

Stereo audio opened and attached; all copied frames were accepted, with about
1.27 seconds cumulative padding and no host-audio fault logged. These counters
do not measure speaker output. After the requested shutdown, the frontend forced
guest stopping and a guest panic was logged before cleanup completed and the
process exited with code 0. This was a shutdown exception sequence, not an
observed gameplay crash; the run is not described as panic-free.

The layer was enabled only for this child process. No global layer installation,
driver change or persistent Vulkan environment override was made; title and
global configuration hashes were unchanged. Raw diagnostic/game captures remain
private. Build 2's published assets retain their original bytes and validation
scope; these later findings are documented here rather than rewriting them.

## Build 3 candidate 1

The unreleased integration was built from
[`f0fe835fd24068c4901945345293239ecc97a048`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/f0fe835fd24068c4901945345293239ecc97a048).
It combines the presentation-transfer, texture-transfer and vertex-stride
corrections in draft PRs [10](https://github.com/ZenKhalil/Lichii-Orbitals/pull/10),
[12](https://github.com/ZenKhalil/Lichii-Orbitals/pull/12) and
[9](https://github.com/ZenKhalil/Lichii-Orbitals/pull/9), plus the default-off
texel metadata diagnostic in [PR 11](https://github.com/ZenKhalil/Lichii-Orbitals/pull/11).
Two compile corrections to that diagnostic are included in the recorded source.
The integrated regression filter passed 124 cases and 51,827,444 assertions;
38 packaged PE files had no missing regular or delay imports on the test host.

The separate 1,006.6-second validation run used the same pinned layer and
confirmed Core Checks and Synchronization. It reached co-op gameplay and
movement after opening skips. All captured streams reached EOF without
truncation or I/O failure. No FPS comparison is made with validation enabled.

| Finding | Recorded reports | Interpretation |
|---|---:|---|
| Buffer-copy write-after-write | 20 | All reported `vkCmdCopyBuffer` after `vkCmdCopyBuffer`, with no covering write barrier; [#13](https://github.com/ZenKhalil/Lichii-Orbitals/issues/13) |
| Compute storage texel numeric type | 20 | Still present; exact pipeline/view/descriptor matches were obtained for all 20 |
| Vertex uniform texel numeric type | 20 | Still present; no exact metadata match before the shared trace budget was exhausted |
| Depth-read and indexed/non-indexed stride faults | 0 | Not recorded in this run; not proof that every path is correct |
| Null Win32 instance handle / private-data feature | 2 / 1 | Startup ownership remains under investigation |
| 3D image layer-count forward-compatibility warning | 20 | Maintenance9 remains disabled |

There were 63 errors, 20 warnings and three status messages. Four categories
reached the 20-message cap. In particular, buffer-copy reports consumed the
write-after-write quota, so later absence of clear reports cannot prove that
all clear hazards are fixed. Build 2's quota was already consumed by other
write-after-write reports; these buffer reports alone do not establish a new
regression. Adjacent current copy ranges also do not identify the prior
overlapping range, which the messages did not include.

All 20 storage mismatches matched one compute pipeline and its actual configured
views, descriptor coordinates and bounded metadata. The captured declaration was
typeless, compiled as UINT, write-only and non-atomic at the stage level; views
were signed or floating point. This narrows the investigation but does not prove
that the complete shader has exactly one descriptor or establish the cause of
the visible glare. Graphics metadata needs its own quota: compute rows exhausted
the original budget, and compute metadata must not be attributed to the graphics
pipeline. Reinterpreting all views as UINT is not an acceptable correction.

The validation run recorded one preserved 240-frame admission retry, with
1.526 seconds cumulative padding and no host audio fault. The retry retained
the queued data; the counters do not show a dropped block. A guest panic followed
the requested shutdown and frontend ForceStop, before cleanup and OS exit code
0. This is a shutdown caveat, not an observed gameplay crash.

### Normal gameplay comparison

The same candidate then ran with validation, texel tracing and heavy profiling
disabled. The complete opening played without skips, both characters reached
the room and both subsequently moved. Settings remained 1×, Accurate CPU/GPU,
FIFO, PAUSE waits and full shader preload 0 on the same i7-12700F / RTX 3080 host.

| Window | Duration | FPS | Frame time p99 |
|---|---:|---:|---:|
| Opening, middle | 30 seconds | 29.92 | 36.09 ms |
| Opening, later | 20 seconds | 30.00 | 35.70 ms |
| Initial room, unchanged view | 30 seconds | 24.69 | 48.11 ms |
| Initial room, same-view repeat | 20 seconds | 24.57 | 48.14 ms |

The opening windows were both cutscene-only. Their enclosing audio intervals,
and those of both room samples, added no padding, retry or queue-full event.
Audio counters do not measure audible A/V synchronization. Room performance
remains below 30 FPS and below Build 2's approximately 25.4 FPS measurements;
these results do not support a performance improvement.

The normal run lasted about 22 minutes 9 seconds. All 61,984,752 copied audio
frames were admitted, with 296 ms cumulative padding, no queue-full/retry event
and no host audio fault. No new native shader rejection or guest panic was
logged; the same 135 known startup errors remained. Frontend ForceStop followed
the requested close, then cleanup and independently verified OS exit completed.
The title and global configuration hashes were unchanged.

Near the control consoles and damaged machinery outside the window, stationary
left-camera snapshots alternated between ordinary lighting and broad saturated
cyan/white, red, magenta or yellow glare. After moving the second character
toward the same machinery, broad green and blue glare also appeared in the
right view. Both cameras showed the same machinery from different positions,
not identical pixels or sight lines. This reproduces strong glare on both halves
in this candidate; it does not prove an exclusively left-side defect, identify
the cause, or establish that it is the same phenomenon as the user's earlier
jitter and static lines. Those reports remain unresolved.

Build 2 remains the latest recommended published release. Candidate 1 is retained
as a separately identified test artifact; source correctness fixes, gameplay
performance and visual outcomes remain separate acceptance checks.
