# Numbered build history

## Build 3 — latest and recommended

[Released 2026-11-09 as a pre-release](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-3).
Source/tag:
[`726551be31ceffb061c8f193b9c336727d85da85`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/726551be31ceffb061c8f193b9c336727d85da85)
(`build-3`). This is the exact numbered binary's source; later documentation or
recommendation updates do not relabel it.

Recommended for demonstrated Vulkan transfer/order, stride-state and Win32
surface corrections, plus narrow numeric-type handling and conflicting-capture
serialization refusal. This does not resolve every numeric mismatch, intermittent
visual artifact or the room FPS deficit.

| Check on this numbered binary | Result |
|---|---|
| Selected native regressions | 174 cases, 53,298,980 assertions passed; overlapping focused two-uniform selection: 2 cases, 500,685 assertions passed |
| Compiler/cache selection | 96 cases, 448,273 assertions passed |
| Opening/room route | Opening reached, then skipped; stationary room: 25.031270 FPS over 30 seconds and 25.277959 FPS over 20 seconds |
| Movement and visual observation | Both players walked; Player 1 turned the camera. Inspected views were intact; no matched machinery-view comparison or artifact-fix claim |
| Audio | Stereo 48 kHz; last logged copied = accepted = 19,560,224 frames, 245.33 ms padding, zero full/retry events; no host audio fault |
| Shutdown | Normal UI close, cleanup and verified OS exit; no OS kill or guest panic |
| Package/import checks | 38 executable/DLL files checked; no missing regular or delay imports on the test host; public projection retains tested binary bytes |

GUI SHA256: `6f17cb0fcbfa246e40aa9be370643885eeee2a36bf9aacfa91b1ba0aa48acb2e`.
CLI SHA256: `45b7e6398a31dfbb2317c56e21a363bbe8bbd3ccdc72f95dba871da1dacbeb38`.
The 446.87-second launch-to-observed-exit smoke used no Vulkan validation layer.
The wider audio brackets around both room windows added no padding, deferred events,
full-queue events or retries. Last logged totals precede the close request; they are
not exact post-close totals or an audibility test. The same 135 known startup errors
remained, with no new error or native graphics rejection. This numbered run did not
test the full opening or six-channel output.

Separate historical Candidate 7 evidence, source `028ea242`, completed the
opening without skips, room measurements, camera movement and normal shutdown
with both experiments off. Opening samples were 29.59 / 29.45 FPS; stationary-room
samples were 24.28 / 25.00 FPS. See [its runtime report](C7_RUNTIME_RESULTS.md).
Those are candidate windows, not measurements of the newly numbered executable.
No sustained 30 FPS or 60 FPS result is established.

PAUSE2 and MULTI_UNIFORM2 remain unset. The one/two/one-PAUSE comparison retained
the one-PAUSE default after the two-PAUSE arm was slower. The enabled two-uniform
validation run selected compatible types for one eligible target but failed
shutdown; its cause remains open in [#34](https://github.com/ZenKhalil/Lichii-Orbitals/issues/34).
Normal smoke, synthetic GPU values and instrumented target checks retain their
separate scopes. Audio endpoint-loss recovery and remaining graphics/joint-resource
numeric mismatches are not claimed fixed.

Release assets:

- [Windows player ZIP](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-3-windows-x86_64.zip).
- [Exact Build 3 application source](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-3-source.tar.gz).
- [Shared dependency sources](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-1-dependency-sources.zip).
- [Checksums](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/SHA256SUMS-build-3.txt).

Current dependency source/header/generated inputs, build recipes/options and all 35 runtime DLLs were checked for reuse. The shared supplement retains
its Build 1 audit/reconstruction metadata and SHA256
`f93da02b3a87bbfe1420baae7b36c9a6b24d5da27db40060b2d86e241a3c4885`;
Build 3 application source is separate. No bit-identical rebuilt-static-library
claim is made. Upstream updates and heavy diagnostics are off; preload 0 and
WAITPKG-disabled one-PAUSE are the packaged defaults. Build 1/2 artifacts and source
tags remain unchanged and available as historical rollback points.

## Build 2 — previous numbered rollback

[Released 2026-11-09 as a pre-release](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-2).
Source/tag: [`c7ada3ec4119d69fce7290e8ec6e09b847263972`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/c7ada3ec4119d69fce7290e8ec6e09b847263972)
(`build-2`). This identifies the source used for the numbered binaries, independently
of subsequent merge or documentation commits.

Build 2 corrects the Vulkan viewport dirty dependency after a depth-mode-only
update, addressing [#3](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3).
The actual linked regression failed the missing dependency assertion before the
fix and passed all 343 assertions across four cases afterward. The optional
viewport observer is disabled by default. Build 2 was recommended at release for this
verified correctness fix and the retained startup/audio behavior; no FPS
improvement or cure for the reported left-view artifacts is claimed.

| Check on this numbered binary | Result |
|---|---|
| Regression tests | 115 cases, 51,826,174 assertions passed |
| Opening | Completed without skips; a 30-second middle window measured 29.97 FPS |
| Initial two-player room | 25.40 FPS over 30 seconds; 25.35 FPS over 20 seconds; enclosing audio samples added no padding |
| Stereo | 1,113.456-second session, co-op gameplay and movement, normal exit |
| Six-channel output | Separate 146.948-second title/PLAY LOCAL menu check; normal exit; no six-channel gameplay or cinematic measurement |
| Package imports | 38 executable/DLL files checked; no missing regular or delay imports on the test host |

Measurements used the same Core i7-12700F / RTX 3080 host, 1× resolution,
Accurate CPU/GPU settings, FIFO and PAUSE monitor waits. The viewport observer
and heavy profiles were off in the reported normal run. A separate 25.34 FPS
window crossed the natural opening-to-gameplay transition; it is not a pure
cutscene or fixed-room result. These windows are not proof of an improvement
over Build 1 or stable 30 FPS throughout.

The stereo run accumulated 14,880 padded audio frames (310 ms at 48 kHz), and
the six-channel check accumulated 2,448 (51 ms). No host audio fault, new native
shader rejection or guest panic was logged. Known startup reports remain,
including Fermi source-depth assertions and three disk-cache compute replay
failures. No reported left-view artifact was captured, which does not establish
that intermittent artifacts are fixed. The separate observer run saw no
depth-mode transition and therefore did not dynamically exercise the repaired
dependency.

[#1: left-view jitter, glare and static lines](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1)
and [#2: initial-room performance below 30 FPS](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2)
remain unresolved. Later chapters, all graphics paths and all audio devices
remain unverified. The earlier candidate's full six-channel opening remains
historical evidence, not coverage of this numbered binary.

Release assets:

- [Windows player ZIP](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-2-windows-x86_64.zip).
- [Exact Build 2 application source](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-2-source.tar.gz).
- [Shared dependency source supplement](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-1-dependency-sources.zip).
- [SHA256SUMS-build-2.txt](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/SHA256SUMS-build-2.txt).

The dependency supplement is unchanged from Build 1 (SHA256
`f93da02b3a87bbfe1420baae7b36c9a6b24d5da27db40060b2d86e241a3c4885`).
Dependency source/header/generated inputs, configured options, GUI/CLI link-input
lists and all 35 runtime DLLs were checked against Build 1. The supplement retains
its original audit and reconstruction qualifications; this does not claim that
rebuilt static libraries are byte-identical. Build 2's application source is the
separate archive above. Selective pipeline preload and PAUSE waits remain the
packaged defaults, with heavy diagnostics off; user settings and explicit
environment variables take precedence. Build 1's assets and source identity
remain unchanged.

## Build 1 — previous numbered baseline

[Released 2026-11-09 as a pre-release](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1).
Source/tag: [`e28cbb16fd4d0fc4103345b79172e8d97d1e97e6`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/e28cbb16fd4d0fc4103345b79172e8d97d1e97e6)
(`build-1`). The tag and release source archive identify the code actually built;
later documentation commits do not change the executable.

Recommended over the earlier local candidates for the validated startup and
audio corrections, memory-coherency fixes and measured CPU monitor baseline.
This recommendation does not imply complete rendering correctness or stable
30 FPS throughout. See [change notes](ORBITALS_CHANGES.md) for implementation scope.

| Check on this numbered binary | Result |
|---|---|
| Regression tests | 111 cases, 51,822,609 assertions passed |
| First presentation | 12.41 seconds; not full gameplay readiness |
| Opening cutscene | 30.04 FPS over 20 seconds |
| Initial two-player room | 25.15 FPS over 30 seconds; 25.24 FPS over 20 seconds |
| Stereo | Reached co-op gameplay; both players walked; normal exit |
| Six-channel output | Separate 143-second startup/title/Play Local check; normal exit; no gameplay/FPS measurement |
| Package imports | 38 executable/DLL files checked; no missing regular or delay imports on the test host |

Performance measurements used one Core i7-12700F / RTX 3080 system, 1× resolution,
Accurate CPU/GPU settings, FIFO and the packaged PAUSE wait policy. Different
scenes/windows are not interchangeable benchmarks. Both room views appeared
intact in the recorded snapshots, which does not rule out intermittent artifacts.

The stereo run accumulated 19,024 padded frames (about 396 ms at 48 kHz); the
six-channel check accumulated 2,752 (about 57 ms). No host audio fault, panic or
new native shader rejection was logged. Known startup errors remain, including
disk-cache compute replay failures and Fermi source-depth assertions.

Full-opening six-channel playback was validated on the preceding candidate
(`39ae332b8d89b711076cacd74a673b4e2af11cf97a8468db8dff71929778c3d0`, GUI SHA256).
That is a different executable and broader playback scope than this numbered
build's six-channel menu check. Later chapters and ship entry remain untested.

Unresolved in Build 1:

- [#1: intermittent left-view jitter, glare and static lines](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1).
- [#2: initial-room performance below 30 FPS](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2).
- [#3: viewport depth can remain stale after a depth-mode-only update](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3). This source defect remains unfixed in Build 1; its connection to #1 is unconfirmed.

The release provides the Windows player ZIP, exact application source, dependency
source supplement and `SHA256SUMS-build-1.txt`. Its defaults use selective pipeline
preload and PAUSE waits; heavy diagnostics are off. Per-user settings and explicit
environment variables take precedence. Releases keep their original assets and
source identity; future executable changes receive a new build number.
