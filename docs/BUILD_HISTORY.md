# Numbered build history

## Build 1 — latest and recommended

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
- [#3: viewport depth can remain stale after a depth-mode-only update](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3). This source defect is unfixed; its connection to #1 is unconfirmed.

The release provides the Windows player ZIP, exact application source, dependency
source supplement and `SHA256SUMS-build-1.txt`. Its defaults use selective pipeline
preload and PAUSE waits; heavy diagnostics are off. Per-user settings and explicit
environment variables take precedence. Releases keep their original assets and
source identity; future executable changes receive a new build number.
