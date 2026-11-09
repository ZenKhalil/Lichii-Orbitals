<!--
SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2018 yuzu Emulator Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Lichii Orbitals

An experimental Lichii fork focused on Orbitals startup, rendering, local co-op
and audio correctness. It builds on Lichii, Eglantine, Eden, yuzu and Sudachi.
It is independent of those projects and is not affiliated with Nintendo.

## Builds

| Build | Status | Recommendation |
|---|---|---|
| [Build 1](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1) | Pre-release · Latest · Recommended | Validated startup/audio fixes and the tested memory/monitor corrections; rendering and performance issues remain open |

[Download Build 1 for Windows x64](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-1/Lichii-Orbitals-build-1-windows-x86_64.zip).
The [release](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1)
also includes application source, dependency source and checksums.
[Build history](docs/BUILD_HISTORY.md) records each numbered binary's validation.
**Latest** identifies the newest published build; **recommended** identifies
the tested choice. A newer diagnostic build does not automatically replace it.

Extract the complete Windows archive and run `lichii.exe`. Existing Eden data
directories remain in use; no save migration is required. Games, keys, firmware,
saves and shader caches are not included. Use your own lawfully obtained files.

## Current scope

Build 1 passes the original logo freeze and reaches local co-op
gameplay. It includes repairs for audio-ring purging, timing after pauses,
GPU query completion, shader-source memory coherency and surround stream
creation. Reviewed monitor-lock changes improve the tested room's performance.
See [change notes](docs/ORBITALS_CHANGES.md) for the evidence and limitations.

The numbered binary measured **30.04 FPS** in an opening cutscene window and
**25.15 / 25.24 FPS** in two initial two-player room windows on the test machine.
Stereo gameplay and a separate six-channel startup/menu check passed. Full-opening
six-channel playback was checked on the preceding candidate, not repeated on
Build 1. These checks do not establish stable 30 FPS throughout or complete
rendering correctness. Brief audio underfills and loading/streaming stalls remain.

Known issues still present in Build 1:

- [Left-view jitter, glare and static lines (#1)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1): reported intermittently; cause unconfirmed.
- [Initial-room performance below 30 FPS (#2)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2).
- [Stale viewport depth after a depth-mode-only update (#3)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3): demonstrated source defect; its connection to the reported left-view artifacts is unconfirmed.

Build 1 is recommended over the earlier local candidates for its validated
startup/audio fixes and measured monitor baseline. Later chapters, all graphics
paths and all audio devices have not been validated.

## Reporting issues

Use this repository's [issues](https://github.com/ZenKhalil/Lichii-Orbitals/issues).
Include the numbered build, game edition/version, hardware, driver, relevant
settings and steps to reproduce. Distinguish a reproducible fault from an
unconfirmed observation. Review any log excerpt for personal information before
sharing it. Do not upload game code, shaders, captures containing game assets,
keys, firmware, saves or private account data.

## Building and contributing

Start with the [Windows build notes](docs/ORBITALS_BUILD.md),
[contributor guide](CONTRIBUTING.md) and [coding guidelines](docs/Coding.md).
The GUI CMake target retains the name `yuzu`; its Windows output is `lichii.exe`.
Optional diagnostics are compiled in but disabled unless explicitly enabled.
Automatic updates from another fork are disabled for numbered builds.

## Provenance and licenses

History is preserved from
[ghzeni/Lichii-anime-coop at 85ef8c2](https://github.com/ghzeni/Lichii-anime-coop/commit/85ef8c2d44988516cb930f3e2fed5bd9e5251ef9),
including the original projects' contributions and authorship. Local changes
are recorded in subsequent commits; this fork does not claim authorship of
upstream code. Original copyright notices and third-party licenses remain.

See [LICENSE.txt](LICENSE.txt), [LICENSES](LICENSES), [.reuse/dep5](.reuse/dep5)
and per-file notices. Runtime dependencies have their own applicable licenses;
release assets include the corresponding notices and source information.
Lichii artwork in [dist/branding](dist/branding) was created by YOB (yobslab).
