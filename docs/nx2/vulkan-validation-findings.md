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
