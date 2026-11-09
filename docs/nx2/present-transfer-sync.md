# Presentation image transfer synchronization

This correction is developed separately from the PAUSE2 experiment on
published source commit `9a1f589dfa5762623d1f637550f7c94118e594e0`. It changes
host Vulkan synchronization, not guest GPU accuracy or command ordering. It
addresses the presentation-clear portion of
[issue 6](https://github.com/ZenKhalil/Lichii-Orbitals/issues/6); the separate
texture-upload/depth-load synchronization findings are not changed here.

## Observed defect

Synchronization validation reported a `WRITE_AFTER_WRITE` hazard when a
presentation image clear followed an image layout transition. The transition's
destination scope covered shader reads and color attachment accesses, but the
clear requires a transfer write dependency. The production helper emitted those
exact insufficient masks. Its upload path also used them before and after a
buffer-to-image copy. The current wrapper preserves these masks when converting
legacy barriers to synchronization2 barriers.

The [Vulkan synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-image-layout-transitions)
requires layout transitions to precede subsequent image accesses. The
[pipeline-stage definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineStageFlagBits2.html)
include image clears in `CLEAR`, covered by `ALL_TRANSFER` (the legacy
`TRANSFER` bit). Graphics/compute stages do not cover that operation.

The reviewed [pinned upstream implementation](https://github.com/zortos293/Lichii/blob/ad477ef1f4b54ed48db6ce73cc990a141637e55b/src/video_core/renderer_vulkan/present/util.cpp)
contains the same missing scopes. A bounded check of the upstream `nx2` file
also found them unchanged; this is not a port of a separate upstream fix.

## Prepared correction

- An undefined source has no previous access to make available.
- Transfer-destination layouts use transfer stages and transfer-write accesses
  on the corresponding side of the dependency.
- Initialization clears transition to transfer-destination layout, clear there,
  then transition to general layout with visibility for the existing consumers.
- General-to-general presentation dependencies retain their existing masks.

Image handles, single color mip/layer ranges, queue-family ownership, clear
values and later consumers remain unchanged. The helper's existing upload
transitions receive the same transfer coverage. This is not a general layout or
resource-usage tracker and does not claim to repair other reported hazards.

## Linked regression results and remaining validation

The new `[present_transfer_sync]` cases call the actual linked helpers and
command wrapper with captured Vulkan dispatch callbacks, for both synchronization2
and legacy dispatch. They check clear command order, layout and zero value,
pre/post dependency scopes, subresources, upload transition endpoints and the
unchanged general-to-general control. They do not create a driver/device or
exercise `UploadImage` allocation, staging or scheduler lifetime.

Against the unchanged implementation, two of the three cases failed with 14
failed assertions; the general-to-general control passed. The upload case
demonstrated the missing transfer scopes through both dispatch paths. The clear
case demonstrated the absent post-clear barrier. After applying only the helper
correction, all three cases passed with 210 assertions. The combined run with
the existing regression filter passed 118 cases and 51,828,430 assertions.

These are linked command-construction tests, not driver execution. A separately
identified candidate still needs a supervised synchronization-validation
rerun of the presentation paths and normal visual/audio smoke checks before
release. An absent message after the validation duplicate cap is not a clean
result; capture a fresh run and inspect coverage.

No connection to the intermittent left-view jitter, glare or static lines is
established, and no FPS improvement is claimed. Build 2 remains the published
recommendation while this correction is under test.
