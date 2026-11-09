# Build 3 candidate 1: Vulkan correctness

This integration is an unreleased test candidate. Build 2 remains the latest
recommended release until a successor passes its own runtime checks. The
candidate does not establish a fix for intermittent left-view artifacts or
stable 30 FPS.

It combines four independently reviewed changes:

| Change | Source commit | Purpose |
| --- | --- | --- |
| Presentation transfers | `1b502a6282263b2a088ed089f46dd1229aa3dce3` | Include transfer writes when initializing and clearing presentation images |
| Texture transfers | `212dd0002de65c0b72a090a21a2d70994bbc8783` | Cover graphics, compute and transfer consumers, including early depth tests |
| Vertex stride state | `3f40f493bf4efe6adb308637dc43d5fc0ba6d738` | Declare stride state when buffer bindings update it |
| Texel metadata | `2b8cd711e98299d340075bdb2cecc65716c73c19` | Add bounded, opt-in metadata for the remaining numeric-type investigation |

Before integration, the presentation recorder tests failed on the unchanged
code and passed with the correction. The texture recorder tests likewise
reproduced both failing cases and passed after correction; their combined
regression filter passed 120 cases. These are actual command-recording checks
without a Vulkan device, not GPU image-content tests.

At this source snapshot, the integrated production build, four additional texel
diagnostic cases, fresh-process selector checks and game runs are pending.
Historical results in the individual change documents describe their own
source snapshots. A package must retain its exact source commit and separate
test/runtime receipts.

The next runtime checks are clear/depth synchronization and indexed/non-indexed
stride validation, followed by pipeline/view correlation for texel mismatches.
Validation-layer timing is excluded from performance comparisons. Normal tests
must separately check the opening, split-screen scene, movement and audio.

`EDEN_NX2_TEXEL_TYPE_TRACE` remains off by default. Full shader preload remains
zero. This candidate retains the accepted single-PAUSE setting; the experimental
second-PAUSE change is not included. User data and existing release packages
are preserved.
