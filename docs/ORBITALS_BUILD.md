# Building Lichii Orbitals on Windows

Use the exact clean source commit named by a numbered build. Do not copy private
build caches, user data or local revision override files into it. The inherited
[general guide](Build.md) and [dependency guide](Deps.md) cover other environments,
which have not received the same gameplay validation as this Windows candidate.

## Tools and dependencies

The tested setup uses x64 clang-cl/LLD 22.1.8, Visual Studio 2022 build tools,
Windows SDK 10.0.22621.0, Qt 6.8.3 MSVC x64, CMake, Ninja, Python and glslang.
Qt Charts and SVG are required. Set `LLVM_ROOT`, `QT_ROOT` and
`GLSLANGVALIDATOR` to installed paths; put CMake and Ninja on `PATH`.

Dependency identities and patches are declared in `cpmfile.json` and `.patch`.
The AV1 FFmpeg binary package has a separate source identity from the generic
FFmpeg manifest entry. Release source bundles must include its exact source,
patches, configuration and notices. Dependency substitutions need a new build
and validation; they must not silently replace a measured artifact.

## Configure and build

From the repository root in PowerShell:

```powershell
. ./tools/windows/load-msvc-env.ps1
$configureArgs = @(
    '-S', '.', '-B', 'build', '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_C_COMPILER=$env:LLVM_ROOT/bin/clang-cl.exe",
    "-DCMAKE_CXX_COMPILER=$env:LLVM_ROOT/bin/clang-cl.exe",
    "-DCMAKE_LINKER=$env:LLVM_ROOT/bin/lld-link.exe",
    "-DCMAKE_AR=$env:LLVM_ROOT/bin/llvm-lib.exe",
    '-DCMAKE_C_FLAGS=-DWIN32 -D_WINDOWS -DNOMINMAX -flto=thin',
    '-DCMAKE_CXX_FLAGS=-EHsc -DWIN32 -D_WINDOWS -DNOMINMAX -flto=thin',
    '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO /MAP /OPT:REF /OPT:ICF',
    '-DCMAKE_JOB_POOLS=orbitals_link=1', '-DCMAKE_JOB_POOL_LINK=orbitals_link',
    '-DBUILD_TESTING=ON', '-DYUZU_TESTS=ON', '-DYUZU_CMD=ON',
    '-DYUZU_ROOM=OFF', '-DYUZU_ROOM_STANDALONE=OFF',
    '-DYUZU_USE_BUNDLED_QT=OFF', "-DCMAKE_PREFIX_PATH=$env:QT_ROOT",
    "-DGLSLANGVALIDATOR=$env:GLSLANGVALIDATOR",
    '-DYUZU_USE_BUNDLED_SDL3=ON', '-DUSE_DISCORD_PRESENCE=OFF',
    '-DENABLE_UPDATE_CHECKER=OFF', '-DLICHII_BUILD_TAG=',
    '-DLICHII_BUILD_PLATFORM=windows-x86_64'
)
cmake @configureArgs
cmake --build build --target yuzu yuzu-cmd tests --parallel 12
```

Reduce parallel jobs to fit available memory. Release builds retain `/O2 /Ob2`
and ThinLTO. Outputs are `build/bin/lichii.exe` and `build/bin/eden-cli.exe`.
Packaging needs Qt runtime deployment, dependency license/source material and
a SHA256 manifest. Exclude tests, private logs, settings, games and caches from
the normal player archive.

## Validation and numbering

The current development source's combined Catch2 filter is:

```powershell
./build/bin/tests.exe '[buffer_ports],[gpu_dirty_memory],[sysmix],[sysmix_sink],[microphone],[shader_invalidation],[report_fence],[timing_pause],[dynarmic_spin_lock],[native_macro],[macro],[maxwell_fast_method],[gpu_split_dependencies],[nx2_debug_server],[split_fence_wait],[viewport_dirty_state]'
```

The four `[viewport_dirty_state]` cases supplement the 111-case Build 1 filter.
They call the complete production dependency builder; they do not submit Vulkan
commands or establish the cause of a visual artifact.

For a numbered release, set its exact `LICHII_BUILD_TAG`, such as `build-1`, and
record the actual source commit, generated revision metadata, compiler and
dependencies. Clear the tag for later unnumbered work; CMake retains old cache
values. The numbered executable needs its own regression and runtime smoke
checks. Automatic updates from upstream repositories remain disabled.
