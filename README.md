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
| [Build 3](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-3) | Pre-release · Latest · Recommended | Verified Vulkan command/state repairs and narrow texel-type handling; rendering, audio-recovery and performance issues remain |
| [Build 2](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-2) | Pre-release · Previous rollback | Retains the viewport dependency correction and its recorded startup/audio validation |
| [Build 1](https://github.com/ZenKhalil/Lichii-Orbitals/releases/tag/build-1) | Pre-release · Historical baseline | Original numbered startup/audio and memory/monitor corrections |

[Download Build 3 for Windows x64](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-3-windows-x86_64.zip).
The release also provides [exact application source](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-3-source.tar.gz),
[dependency sources](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/Lichii-Orbitals-build-1-dependency-sources.zip)
and [checksums](https://github.com/ZenKhalil/Lichii-Orbitals/releases/download/build-3/SHA256SUMS-build-3.txt).
Build 3's binary source is
[`726551be`](https://github.com/ZenKhalil/Lichii-Orbitals/commit/726551be31ceffb061c8f193b9c336727d85da85).
Later documentation commits do not change that identity. The reused dependency
supplement retains its original Build 1 name and reconstruction notices;
current dependency source/header/generated inputs, build recipes/options and all 35 runtime DLLs were checked for reuse.

Extract the complete archive and run `lichii.exe`. Existing Eden user data remains
in use; no save migration or cache deletion is required. Games, keys, firmware,
saves and shader caches are not included. Use your own lawfully obtained files.
[Build history](docs/BUILD_HISTORY.md) records each numbered binary's tests and
runtime scope. Latest means the newest published build; recommended means the
tested choice, not complete compatibility.

## Current scope and limits

Build 3 fixes demonstrated Vulkan presentation/texture transfer scopes, reordered
buffer-write ordering, dynamic vertex-stride declarations and Win32 surface
instance handling. Eligible texel families select matching numeric types without
reinterpreting prepared views. Known-conflicting compiler captures are refused
before disk-cache append. Existing audio, memory and viewport repairs are retained.
See [change notes](docs/ORBITALS_CHANGES.md) for evidence and scope.

The exact numbered binary passed a stereo startup, initial-room movement and normal-shutdown smoke with both experiments off; its 30- and 20-second room windows measured 25.03 / 25.28 FPS after skipping the opening.
This is a correctness release: no FPS improvement or visual-artifact cure is
claimed. Sustained 30 FPS in the initial co-op room has not been achieved;
60 FPS is not an achieved target either. Earlier candidate measurements are
[reported separately](docs/C7_RUNTIME_RESULTS.md), not substituted for the numbered
binary's smoke test.

Both experimental switches remain off. The two-PAUSE comparison was slower;
the exact-two uniform experiment showed compatible target bindings but its
enabled diagnostic run failed shutdown ([#34](https://github.com/ZenKhalil/Lichii-Orbitals/issues/34)).
One PAUSE, selective preload and disabled heavy diagnostics remain the defaults.

Open investigations include:

- [Intermittent split-screen jitter/glare/banding (#1)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/1) and [room performance (#2)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/2).
- [Remaining graphics and multi-resource texel mismatches (#7)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/7).
- [Selected audio endpoint-loss recovery and shutdown (#16)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/16).
- [Non-unit copy depth (#24)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/24), [historical incomplete cache replay (#25)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/25), and [privateData startup validation (#28)](https://github.com/ZenKhalil/Lichii-Orbitals/issues/28).

Later chapters, every graphics path and device-loss recovery are not validated.
A clean startup or a briefly absent intermittent artifact does not close these issues.

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
