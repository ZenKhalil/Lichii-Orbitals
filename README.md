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
| [Build 2](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-2) | Pre-release · Latest · Recommended | Corrects the verified viewport depth dependency; startup/audio checks passed; rendering and performance issues remain open |
| [Build 1](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1) | Pre-release · Previous baseline | Original numbered startup/audio and memory/monitor corrections; lacks the Build 2 viewport dependency fix |

[Download Build 2 for Windows x64](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-2-windows-x86_64.zip).
The release also includes [exact application source](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-2-source.tar.gz),
the [shared dependency source supplement](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/Lichii-Orbitals-build-1-dependency-sources.zip)
and [checksums](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-2/SHA256SUMS-build-2.txt).
The unchanged dependency supplement retains its Build 1 name and audit metadata;
Build 2's dependency inputs/options and all 35 runtime DLLs were checked against
Build 1. It is separate from Build 2's application source, and is not a claim of
a bit-identical dependency rebuild.
[Build history](docs/BUILD_HISTORY.md) records each numbered binary's validation.
**Latest** identifies the newest published build; **recommended** identifies
the tested choice. A newer diagnostic build does not automatically replace it.

Extract the complete Windows archive and run `lichii.exe`. Existing Eden data
directories remain in use; no save migration is required. Games, keys, firmware,
saves and shader caches are not included. Use your own lawfully obtained files.

## Current scope

Build 2 retains the fixes that pass the original logo freeze and reach local co-op
gameplay. It includes repairs for audio-ring purging, timing after pauses,
GPU query completion, shader-source memory coherency and surround stream
creation. It additionally corrects the Vulkan viewport dirty dependency when the
guest changes depth mode. A linked regression demonstrated the missing dependency
before the fix and passed afterward. This is a correctness update; no FPS
improvement or fix for the reported left-view artifacts is claimed.
See [change notes](docs/ORBITALS_CHANGES.md) for the evidence and limitations.

The numbered binary completed the opening without skips and measured **29.97 FPS**
in a 30-second cutscene window and **25.40 / 25.35 FPS** in two initial two-player
room windows on the test machine. The stereo session lasted about 18 minutes
33 seconds; a separate six-channel startup/menu check passed. Total logged audio
padding was 310 ms and 51 ms respectively; the measured room windows added none.
These checks do not establish stable 30 FPS throughout or complete rendering
correctness. Brief audio underfills and loading/streaming stalls remain.

Known issues still unresolved:

- [Left-view jitter, glare and static lines (#1)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1): reported intermittently; cause unconfirmed.
- [Initial-room performance below 30 FPS (#2)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2).

A subsequent, separately instrumented run also identified Vulkan transfer,
texel-buffer type and vertex-state faults. See the [validation findings](docs/nx2/vulkan-validation-findings.md)
and issues [#6](https://github.com/ZenKhalil/Lichii-Orbitals/issues/6),
[#7](https://github.com/ZenKhalil/Lichii-Orbitals/issues/7) and
[#8](https://github.com/ZenKhalil/Lichii-Orbitals/issues/8).
Their connection to the intermittent visual artifacts is still unconfirmed.

The unreleased **Build 3 candidate 1** removed the recorded stride and depth-read
validation reports in its bounded follow-up run, but buffer-copy ordering and
texel numeric-type errors remain. Normal room samples measured **24.69 / 24.57
FPS**. Strong changing glare was observed in both views near the same machinery;
the cause and connection to the original jitter/static report remain unconfirmed.
See the [candidate results](docs/nx2/vulkan-validation-findings.md#build-3-candidate-1).
It does not replace the recommended Build 2 release.

[The viewport dependency defect (#3)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/3)
is corrected in Build 2. The observer run saw no depth-mode transition, so the
game checks did not dynamically exercise that repair or establish a connection
to the left-view artifacts. Build 2 is recommended for the verified correction
and retained startup/audio behavior. Later chapters, all graphics paths and all
audio devices have not been validated.

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
