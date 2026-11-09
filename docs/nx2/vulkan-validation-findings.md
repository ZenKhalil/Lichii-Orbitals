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

## Build 3 candidate 2

Source [`a75363aace35178af0bdbbdea3cfcce49a20cd68`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/a75363aace35178af0bdbbdea3cfcce49a20cd68)
adds the buffer-upload ordering correction in [PR 14](https://github.com/ZenKhalil/Lichii-Orbitals/pull/14)
and independent graphics/compute trace budgets in [PR 15](https://github.com/ZenKhalil/Lichii-Orbitals/pull/15).
The numbered candidate's combined regression run passed 127 cases and 51,826,409
assertions. All 38 packaged PE files passed the host dependency check. Candidate
1 and Build 2 package bytes were preserved.

### Validation and visual results

A separate 580.6-second Core Checks and Synchronization run used the same pinned
validation layer. It reached co-op gameplay and both-player movement after opening
skips. Both streams reached EOF without truncation or I/O failure. This instrumented
run is not a performance measurement.

| Finding | Reports | Scope |
|---|---:|---|
| Synchronization write-after-write / read-after-write | 0 / 0 | No synchronization category reached its duplicate limit in this run |
| Indexed / non-indexed stride state | 0 / 0 | Not reported through the tested room/movement route |
| Compute storage texel numeric type | 20 | Still present; exact metadata association for all reports |
| Graphics uniform texel numeric type | 20 | Still present; exact metadata association for all reports |
| Null Win32 instance handle / private-data feature | 2 / 1 | Startup reports remain |
| 3D image layer-count forward-compatibility warning | 20 | Maintenance9 remains disabled |

There were 43 errors, 20 warnings and three status messages. Both texel-type
categories and the warning category reached their 20-message limits. Their
counts are bounded emitted reports, not total invalid operations. The absence
of synchronization reports is stronger evidence than Candidate 1's saturated
write-after-write category, but does not prove every transfer path is correct.

The split trace budget exposed the graphics mismatch: a native vertex-stage
uniform texel-buffer declaration compiled as FLOAT was bound to `R32_UINT`.
All 20 graphics errors matched the exact configured pipeline, view and descriptor
coordinates. All 20 compute errors likewise matched UINT storage declarations
bound to signed or floating-point views. This establishes specific type conflicts,
not their contribution to a screen half or visible artifact. A traced descriptor
count of one does not prove the entire pipeline has only one buffer descriptor.

Faint horizontal bands were visible in both room views under validation, and
strong magenta glare appeared in the left view near the damaged machinery.
These observations do not establish equivalence to the original reported static
lines. The validation run recorded a guest panic after the requested close and
ForceStop, before cleanup and OS exit; it is not described as panic-free.

### Normal run

The same binary then ran with validation, texel tracing and heavy profiling off.
The complete opening played without skips, followed by two initial-room samples
at unchanged camera/character positions and movement of both characters. Settings
and clock policy matched the previous normal comparison; no build ran concurrently.

| Window | Duration | FPS | Frame time p99 |
|---|---:|---:|---:|
| Opening, middle | 30 seconds | 29.95 | 41.68 ms |
| Opening, later | 20 seconds | 30.02 | 38.27 ms |
| Initial room | 30 seconds | 24.31 | 49.06 ms |
| Initial room, same-view repeat | 20 seconds | 24.45 | 50.56 ms |

Both opening intervals were cutscene-only. Their enclosing audio intervals and
the first room interval added no padding, retries or queue-full events. The
repeat measurement was also padding-free; its wider audit bracket includes
3.67 ms of padding after the measurement ended. Counters do not measure audible
A/V alignment. These room results do not show an FPS improvement.

After movement, stationary left-camera snapshots alternated ordinary-looking
lighting with broad yellow/orange and green/blue glare at the machinery. The
right camera was farther down the hallway and looked ordinary in the inspected
snapshot; it was not aimed at the same machinery. The visual fault remains open.

The **complete normal run was not fault-free**. At approximately 902.5 seconds,
Cubeb reported output-device invalidation on all three output streams. Windows
audio events independently recorded an NVIDIA endpoint becoming unavailable at
the corresponding time. The guest reported audio error/panic `0x499` at 903.046
seconds, before the UI close request at 930.942 seconds. ForceStop, an audio
renderer termination timeout and an emulation-thread termination attempt followed.
The process remained unresponsive and required guarded OS termination.

No audio-device change was deliberately made during the test. The endpoint-loss
trigger and shutdown hang cause remain unknown; this does not establish a
Candidate 2 graphics regression. [Issue 16](https://github.com/ZenKhalil/Lichii-Orbitals/issues/16)
tracks device recovery and shutdown behavior. Earlier clean measurement windows
must not be used to characterize the entire run as clean.

Build 2 remains the latest recommended published release. Candidate 2 is an
unreleased correctness investigation with unresolved rendering, performance and
audio-device-loss behavior. Raw logs, endpoint identities and game data remain
private.

## Build 3 candidate 3

Source [`d969d8e90880897f96c5ef3797dd732e7bb1d0ab`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/d969d8e90880897f96c5ef3797dd732e7bb1d0ab)
adds the Win32 surface-instance correction in [PR 18](https://github.com/ZenKhalil/Lichii-Orbitals/pull/18),
immutable cold/warm compute replay in [PR 19](https://github.com/ZenKhalil/Lichii-Orbitals/pull/19),
and narrowly eligible typed storage-write pipelines in [PR 20](https://github.com/ZenKhalil/Lichii-Orbitals/pull/20).
The actual numbered test binary passed 140 combined cases and 51,840,348
assertions. Separate replay, family and compiler/cache selections passed; these
overlap the combined selection and are not additive. All 38 packaged PE files
passed the dependency check. Earlier numbered packages remain unchanged.

An actual-emitter headless Vulkan check exercised eight storage-write cases.
Mandatory integer and finite-float values and surrounding canaries matched, with
no Core or Synchronization validation error. Special floating-point patterns also
matched on this RTX 3080, but are observations rather than a portable bit-exact
guarantee. This checks the narrow emitted store path; it does not establish queued
pipeline lifetime, buffer-read correctness or gameplay pixels.

### Validation run

A separate 594-second run positively verified the pinned Vulkan layer, Core
Checks and Synchronization. Both output streams were captured completely through
process exit. It reached the room and both-player movement after opening skips.

| Finding | Reports | Scope |
|---|---:|---|
| Synchronization / vertex stride / Win32 instance | 0 / 0 / 0 | None captured on the tested route |
| Compute uniform texel numeric type | 20 | FLOAT declarations bound to unsigned views |
| Graphics uniform texel numeric type | 20 | FLOAT vertex declaration bound to R32_UINT |
| Private-data feature | 1 | Startup error remains |
| 3D image layer-count forward-compatibility warning | 20 | Maintenance9 remains disabled |

The totals were **41 errors, 20 warnings and three status messages**. Both texel
categories and the warning category reached their duplicate limits. All 40 texel
errors matched exact pipeline/view/set/binding/element metadata; later silence
cannot establish clean rendering.

Eight compute storage controls had compatible compiled and actual host numeric
classes: SINT/R32_SINT, FLOAT/R32_SFLOAT or RGBA32_SFLOAT, and the existing
UINT/RGBA32_UINT fallback. Another 64 metadata rows still showed UINT/R32_SINT
conflicts in programs with stage-wide image atomics, excluded by the narrow
family. The dispatch-error limit had already been reached by uniform-read faults
before these rows appeared. No format reinterpretation hides those conflicts.
Object correlation does not identify a screen half or establish an artifact cause.

Faint horizontal bands appeared in both room views. A broad blue glow beside the
left window appeared in one snapshot and was absent in the next stationary
snapshot. This does not establish that every light effect is unintended or that
it is identical to the original jitter/static report. Validation FPS is not used
as a performance comparison.

Stereo headset output's last logged totals were 26,409,760 frames copied and
accepted, with 1.802 seconds cumulative padding and no queue-full/retry. No host
audio fault, guest panic or native graphics rejection was logged. UI close led
to cleanup and normal OS exit, with no
new Error/Critical entry after close. The 135 recurring startup/cache/stub errors
remain. This run did not exercise recovery from monitor-endpoint loss.

### Normal run

The same binary ran with validation, texel tracing and heavy profiling off,
immediately after the validation run with warm caches. The full opening played
without skips. Both room measurements preceded camera or character movement.
No build or GPU test ran concurrently. Rendering settings and the per-application
clock policy matched the preceding comparison; audio used the connected stereo
headset because the earlier selected monitor endpoint had become unavailable.

| Window | Duration | FPS | Frame time p99 |
|---|---:|---:|---:|
| Opening, middle | 30 seconds | 29.92 | 40.47 ms |
| Opening, later | 20 seconds | 29.78 | 42.44 ms |
| Initial room | 30 seconds | 22.29 | 55.79 ms |
| Initial room, same-view repeat | 20 seconds | 22.00 | 57.12 ms |

All four enclosing audio intervals added no padding, retries or queue-full events.
These counters do not measure audible A/V alignment. The room measurements are
lower than Candidate 2's 24.31 / 24.45 FPS; this comparison does not isolate the
cause. They do not meet the 30 FPS target.

The normal run's last logged audio totals were 31,459,152 frames copied and
accepted, with 286 ms cumulative padding and no queue-full/retry. No host audio
fault, guest panic, native graphics rejection or unavailable typed-family report
was logged during the complete run. The same 135 known
startup/cache/stub errors remain. UI close led to cleanup and guarded normal OS
exit without a post-close Error/Critical entry. Title and global configuration
bytes matched their respective pre-run copies.

First presentation occurred at 12.309 seconds and stereo Open/Attach at
30.074/30.076 seconds. The cache reported 3,852 pipelines at 5.804 seconds,
73,703 skipped graphics entries and 64 background entries, with the known three
disk-replay failures. There is no explicit cache-completion marker. These are
startup milestones, not gameplay-readiness timings: menu interaction included
coordinator delays, and the run used warm caches. They do not establish cold-load
performance or explain the earlier three-minute load.

Faint banding remained visible in the room light cones/back wall. No broad
changing colored glare was seen in the inspected normal snapshots, but the later
camera positions differed from the earlier machinery checks. That absence does
not establish a fix for the intermittent visual problem.

**Candidate 3 is not promoted. Build 2 remains the latest recommended published
release and selected launcher.** Uniform-texel faults, excluded storage cases,
the original visual report, performance and audio-device-loss recovery remain
open. Raw logs, game data and private configuration remain private.

## Build 3 candidate 4

Source [`d133a00d`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/d133a00d7710719892f9645716db4072b69e4691)
adds typed uniform-texel emission in [PR 21](https://github.com/ZenKhalil/Lichii-Orbitals/pull/21),
narrow vertex families in [PR 22](https://github.com/ZenKhalil/Lichii-Orbitals/pull/22),
and compute families in [PR 23](https://github.com/ZenKhalil/Lichii-Orbitals/pull/23).
The test binary from the final candidate build passed 155 cases and 51,888,092
assertions in the combined selection;
the separate compiler/cache selection passed 95 cases and 448,211 assertions.
Overlapping selections are not additive. All 38 packaged PE files passed the
dependency check, and the 35 runtime DLL identities matched the prior candidate.

Nine actual-emitter GPU fetch cases passed mandatory raw integer, finite-float,
missing-component and canary checks with no Core or Synchronization error.
Special floating-point observations remain GPU-specific. These fixtures test
emitted values, not the complete graphics draw, queued lifetime or game pixels.

Transient checksum-download failures required a reviewed local configure-module
override for four exact dependency URLs. Cached archives and installed inputs
were checked; dependencies were not substituted. Compiled application source
matched the stated commit, but the configure module was an additional input.
This candidate is not a public binary release. Any later release must either
include the exact portable override/checksum inputs and instructions in its
corresponding source or rebuild with the original configure module.

### Validation run

The 827-second run positively verified the pinned layer and Core/Synchronization
checks. Both output streams reached EOF without truncation or capture errors.
It reached the room, camera movement and both-player walking after opening skips.

Two observed compute shaders used matching UINT declarations and R32_UINT views,
including a shader that had mismatched in Candidate 3. Other workloads remained
excluded. The known vertex shader explicitly reported `single_fetch=false` at
construction and retained its FLOAT/R32_UINT mismatch. The graphics control quota
had already been consumed by earlier descriptors; absent later controls are not
evidence of a match.

Validation recorded **41 errors and 20 warnings**: 20 compute numeric-type errors,
20 indexed-draw numeric-type errors, one private-data feature error, and 20
depth-layer compatibility warnings. Both texel categories and the warning
category reached their message limits. All 40 texel errors matched exact
pipeline/view/set/binding/element metadata. The 216 trace rows also contained
64 storage mismatches in shaders with stage-wide image atomics; this does not
establish an atomic operation on the mismatched descriptor itself. No
synchronization, vertex-stride or Win32 surface 01307 report was captured on
this route. Bounded capture does not establish universal correctness.

First presentation occurred at 20.222 seconds; stereo Open/Attach at
38.170/38.172 seconds. The last audio counter, at 822.169 seconds, recorded
37,491,584 copied and accepted frames, 1.415 seconds cumulative padding and no
queue-full/retry count. No host audio failure or native graphics rejection was
logged. The same 135 known pre-close errors remained.

Faint bands appeared around both views' light cones/back walls. No broad glare
was seen in these particular snapshots; different viewpoints prevent a fix claim.
No FPS comparison is made under validation. UI close at 822.986 seconds led to
ForceStop at 823.978 seconds and guest panic 1472 at 823.982 seconds. Cleanup
completed at 826.024 seconds and the process exited without OS termination.
This was an after-close panic, with no preceding host audio fault, and is not
described as error-free teardown or attributed to the uniform-fetch change.

### Normal run

The same binary then completed the full opening without skips, with validation,
texel tracing and heavy profiling off. No build or GPU test ran concurrently.
The selected stereo headset, rendering settings and per-application clock policy
matched Candidate 3. Caches were warm after validation.

| Window | Duration | FPS | Frame time p99 |
|---|---:|---:|---:|
| Opening, middle | 30 seconds | 29.81 | 41.51 ms |
| Opening, later | 20 seconds | 29.89 | 42.61 ms |
| Initial room | 30 seconds | 24.83 | 51.04 ms |
| Initial room, same-view repeat | 20 seconds | 25.51 | 46.05 ms |

Both room measurements preceded camera/character movement. All four enclosing
audio intervals added no padding, retries or queue-full events. These counters
do not measure audible A/V alignment. Room FPS was higher than the preceding
Candidate 3 samples but does not isolate a cause or establish stable 30 FPS.

After movement, a closer stationary machinery view alternated broad blue glare,
ordinary-looking lighting, reddish glare and broad blue glare again. The right
camera remained in a different hallway view. This reproduces changing glare
without establishing a specific shader cause, an identical-camera comparison or
that every light effect is unintended. Faint initial-room bands also remained.

The approximately 713-second run's last audio counter, at 707.003 seconds,
recorded 32,320,912 copied and accepted frames, 302.7 ms cumulative padding,
and no queue-full/retry count. No host audio fault, guest panic, native graphics
rejection or unavailable typed-family message was logged. Exactly 135 known
startup/cache/stub errors remained, with no new or after-close Error/Critical
entry. UI close at 708.385 seconds led to cleanup at 711.485 seconds and normal
OS exit without forced termination. Title and global configuration bytes matched
their pre-run copies. This does not exercise audio-device-loss recovery.

First presentation occurred at 15.217 seconds and stereo Open/Attach at
33.003/33.005 seconds. The cache reported 3,852 pipelines at 8.805 seconds,
73,703 skipped graphics entries and 64 background entries. The same three
disk-replay classification failures occurred at 9.698–10.263 seconds; no explicit
cache-completion marker exists. These are startup milestones, not cold-load or
exact gameplay-readiness measurements; menu interaction included coordinator
delays.

**Candidate 4 is not promoted. Build 2 remains the latest recommended published
release and selected launcher.** Rendering, performance, shutdown and audio
device recovery issues remain open. Raw logs and game data remain private.

## Build 3 candidate 5

Candidate 5 source is `59f6372b5958e52ffce224567baac6777c07a759` in
[draft PR #26](https://github.com/ZenKhalil/Lichii-Orbitals/pull/26).
The GUI SHA-256 is
`2a421fdabf95533a9363f4c6c8b59370ae7886aff8a9ea01e148958597b34013`.
It adds feature-supported R16_UINT compute reads, a narrow mixed storage-write
policy, a secondary-handle guard, and bounded exclusion/Streamline diagnostics.
The packaged regression selection passed 161 cases / 51,909,455 assertions;
compiler/cache coverage passed 95 cases / 448,211 assertions. Separate actual
emitter GPU checks covered one R16 case and 18 mixed-store/image-atomic cases,
with no Core/Synchronization error. Those synthetic cases do not establish that
the corresponding game shaders use the new families.

The 891.9-second validation run confirmed the pinned layer and Core/Synchronization
categories. Both output streams reached EOF without truncation. It recorded 41
validation errors: 20 compute texel-type errors, 20 indexed-graphics texel-type
errors and one disabled-privateData-feature error. The 20 layer-count warnings
and both texel categories reached their respective message caps. No synchronization,
stride `08608` or Win32 `01307` report appeared on this route; coverage is bounded.

All 40 texel errors matched recorded pipeline/view/descriptor identities. The
new assessment identifies declaration-count exclusions: two, two and three for
the principal remaining compute shaders, and four for the graphics shader.
Consecutive same-pipeline rows show mixed integer/float view tuples; there is
no explicit Configure transaction ID. Assigning one common numeric class to
these shaders would not cover those observations. Two other compute shaders
explicitly reported eligible, built families and matching UINT/R32_UINT views.
The three later mixed-store targets still emitted 64 incompatible storage rows
and contained uniform declarations excluded by the narrow store policy. Earlier
shaders consumed the eight compatible storage-control tickets; missing later
control rows cannot establish activation.

The returned Streamline requirements did not request privateData. That observation
does not identify the caller responsible for the validation error. A separate
NGX feature-update error, `0xbad00002` at 7.539 seconds, has no established cause.
The usual 135 startup/cache/stub errors remained. All 126 Fermi2D depth operations
used the software path and matched the prior descriptor/configuration/relative
address pattern. This does not establish copied-pixel correctness or a new
slice-copy defect. The three cached-compute replay failures also remained.

First presentation was at 18.785 seconds and stereo Open/Attach at
36.591/36.594 seconds. These are startup milestones, not gameplay-readiness
times. The last logged audio counters, at 886.586 seconds, recorded
40,608,016 copied and accepted frames,
1.875 seconds cumulative padding, and no queue-full/retry event. No host audio
fault or native graphics rejection was found. Validation overhead prevents
using this run as an FPS comparison; counters do not prove audible A/V alignment.

After skipping the opening, both players walked and the left camera rotated.
Faint bands remained in both initial-room light-cone/back-wall views. No broad
changing glare appeared in the inspected snapshots, but different camera
positions prevent a glare-fix claim. Close was requested at 886.865 seconds;
ForceStop at 887.855 seconds preceded panic 1472 and three related error entries.
Cleanup and verified OS exit completed without forced termination. This was
not error-free teardown. Title/global configuration bytes matched across this run.

### Normal gameplay and audio check

The same binary completed the opening without skips, followed by both players
walking and independent camera movement. No validation layer or texel trace was
loaded, and no build/native test ran during its measurements. The cache was warm
after the validation run. Headset output, title configuration and the existing
GPU clock policy matched the preceding normal run.

| Window | Duration | Mean FPS | p99 frame time | Maximum frame time |
|---|---:|---:|---:|---:|
| Opening sample 1 | 30 s | 26.57 | 57.19 ms | 67.60 ms |
| Opening sample 2 | 20 s | 27.17 | 53.49 ms | 60.17 ms |
| Initial stationary split room | 30 s | 25.05 | 47.36 ms | 50.09 ms |
| Same room before movement | 20 s | 24.45 | 48.45 ms | 60.50 ms |

The opening samples were slower than the previous run's approximately 30 FPS.
The cause is unconfirmed; these are not identical cutscene-frame intervals or
a controlled rollback comparison. Room performance remains below the target.
The first enclosing audio interval added 272 padding frames (5.67 ms) and one
deferral; the other three added no padding or deferral. All four had no
queue-full/retry event or Error/Critical entry. These intervals bound the
measurements and do not measure audible A/V alignment.

Faint initial-room bands remained. In the closer machinery view, broad blue
glare coincided with character head-covering reactions without input, so a game
effect may contribute. The second player then moved beside the first. While
opening the close dialog, a large magenta-blue flare covered the left view and
the nearby right view of the same visible machinery stayed clear; the first
character's reacting pose appeared in both views. This strengthens the observed
view asymmetry. Different camera positions/yaw still prevent an identical-pixel
comparison, shader attribution or a claim that every flash is unintended.

The 1,347-second session's last logged audio counter, at 1,342.782 seconds,
recorded 62,849,392 copied and accepted audio frames,
292.7 ms cumulative padding and no queue-full/retry event. All 135 Error/Critical
entries matched known startup/cache/stub signatures. No host audio fault, guest
panic, native graphics rejection, failed-family event or new failure capture
was found. The NGX startup error from validation did not recur. UI close at
1,343.210 seconds led to ForceStop at 1,344.203 seconds, cleanup at 1,345.717
seconds and verified OS exit without forced termination or after-close errors.

First presentation was at 15.197 seconds and stereo Open/Attach at
32.785/32.787 seconds. The cache reported 3,852 pipelines at 8.821 seconds,
73,703 skipped graphics entries and 64 background entries; the three known
cached replay failures remained. These are startup milestones, not cold-load
or exact gameplay-readiness times. Title configuration stayed unchanged.
The application temporarily rewrote shortcut/default metadata at startup;
all non-shortcut settings and the final global configuration bytes matched.

A separate CPU-trace preflight after the four benchmarks was aborted before
recording. No ETL was collected and no profiling, power or sampling setting
changed. It supplies no CPU-bottleneck evidence. No audio-device-loss recovery
was exercised in this headset run.

**Candidate 5 remains unreleased and unpromoted. Build 2 remains the latest
recommended published release and selected launcher.**
