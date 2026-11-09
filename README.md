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
| Build 1 | Preparing the numbered release and its corresponding source | Pending the final numbered-binary checks |

[Releases](https://github.com/ZenKhalil/Lichii-Orbitals/releases) contain numbered
Windows builds when available. Each release records its source commit, checksums,
test results and known limitations. **Latest** identifies the newest published
build; **recommended** identifies the tested choice and explains why. A newer
diagnostic build does not automatically replace the recommended build.

Extract the complete Windows archive and run `lichii.exe`. Existing Eden data
directories remain in use; no save migration is required. Games, keys, firmware,
saves and shader caches are not included. Use your own lawfully obtained files.

## Current scope

The release candidate passes the original logo freeze and reaches local co-op
gameplay. It includes repairs for audio-ring purging, timing after pauses,
GPU query completion, shader-source memory coherency and surround stream
creation. Reviewed monitor-lock changes improve the tested room's performance.
See [change notes](docs/ORBITALS_CHANGES.md) for the evidence and limitations.

The latest candidate played the opening naturally at approximately 30 FPS and
measured approximately 24 FPS in the fixed two-player room on the test machine.
Those are different scenes, not a guarantee of 30 FPS throughout. Later chapters,
all graphics paths and all audio devices have not been validated. A reported
left-view-only problem includes jittering objects, glare and static lines in
the waiting room; its cause remains under investigation. Brief audio underfills
can still occur around loading and movement. Any recommendation over older
local builds is for the validated startup/audio fixes, not complete rendering
correctness or a guaranteed frame rate.

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
