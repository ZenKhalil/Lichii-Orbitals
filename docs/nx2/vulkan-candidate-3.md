# Build 3 candidate 2: Vulkan correctness

This integration is an unreleased test candidate. Build 2 remains the latest
recommended release until a successor passes its own runtime checks. The
candidate does not establish a fix for intermittent left-view artifacts or
stable 30 FPS.

It retains candidate 1 source `f0fe835fd24068c4901945345293239ecc97a048`,
including these four independently reviewed changes and its two diagnostic
compilation corrections:

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

Candidate 1 compiled and passed 124 combined cases, its four texel cases with
the option off and on, and nine fresh-process selector checks. Its bounded
validation run exposed buffer copy write-after-write reports and numeric-type
mismatches. That does not prove a new regression: earlier presentation errors
had filled the same write-after-write reporting limit. The live buffer handle
has not been associated with a particular source recording route.

Candidate 2 adds only these changes to that source:

| Change | Source commit | Purpose |
| --- | --- | --- |
| Reordered buffer uploads | `8596dd080ee0044f8c2b57d26e5a27d7d7dca6c5` | Honor requested transfer-write ordering between upload copies |
| Stage-family trace quotas | `7699856e802d6c9ce516dc59939cc47502869d23` | Preserve graphics metadata permits after compute exhausts its quota |

The actual unchanged buffer recorder failed two cases while its false-barrier
control passed; the correction passed all three cases and 318 assertions.
The extended quota fixture failed three of four cases against the shared-stage
header, including a missing graphics batch after compute exhaustion. The
correction passed four cases and 1,123 assertions with the option off and on.
The combined source gate passed 127 cases and 51,833,695 assertions.

These tests compiled the affected production sources and linked the application
test target. The candidate 2 GUI/CLI build, final package checks and runtime
validation are pending at this source snapshot. A package must retain its exact
source commit and separate test/runtime receipts. Candidate 1 and Build 2
packages remain unchanged.

The next runtime checks require a fresh validation process to assess copy
ordering without inherited category caps, recheck clear/depth synchronization
and stride state, and correlate graphics and compute pipeline/view metadata.
Numeric-type mismatches are not corrected by increasing diagnostic coverage.
Validation-layer timing is excluded from performance comparisons. Normal tests
must separately check the opening, split-screen scene, movement and audio.

The package identity is `build-3-candidate-2`, with distribution status
`candidate` and automatic updates disabled. `EDEN_NX2_TEXEL_TYPE_TRACE` and
other diagnostic switches remain unset by default. Full shader preload remains
zero, and the accepted single-PAUSE policy remains enabled. No second-PAUSE,
immutable compiler-replay or typed shader-family change is included. User data
and existing release packages are preserved.
