# Texture transfer synchronization

This candidate follows the separate presentation-transfer correction. It addresses
the texture-upload and image-copy dependency scopes in
[issue 6](https://github.com/ZenKhalil/Lichii-Orbitals/issues/6). It does not change
guest GPU accuracy, texture formats, copy regions or queue ownership.

## Defect and scope

Synchronization validation reported a depth attachment load after an image layout
transition whose destination scope included late fragment tests, compute and color
attachment output, but not early fragment tests. `CopyBufferToImage` emits that
scope. `CopyImage` uses a similarly incomplete scope and restores its source image
to `GENERAL` with a zero destination access mask. The source patterns establish
dependency gaps; the validation handles have not been traced to individual cache
objects or to the reported left-view artifact.

The [Vulkan render-pass specification](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)
places depth/stencil attachment loads in early fragment tests. The
[synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-image-layout-transitions)
requires layout transitions to be ordered before subsequent accesses. Shader
consumers also extend beyond the old compute-only shader stage scope.

The candidate covers graphics, compute and transfer stages before and after these
two transfer paths. Upload dependencies include prior transfer writes and later
transfer reads/writes as well as shader and attachment access. Both images in an
image copy receive the consumer dependency when returning to `GENERAL`. The
source-image post-copy source access remains zero: the preceding copy reads that
image, while the transfer execution scope orders the following layout transition.

The existing command sequence, layout choices, subresource range calculation,
queue-family indices, copy arguments and scheduler captures remain unchanged.
The barrier range remains the minimum/maximum mip and layer bounds, including
any gaps between individual copy regions; it is not an exact union of regions.
The public command-wrapper path still selects synchronization2 when available
and the legacy barrier entry point otherwise.

## Initialization and caller restrictions

The upload's initialization flag still selects `UNDEFINED` or `GENERAL` as its
old layout. A false flag is also used for reusable multisample scratch images,
so this change does not treat every false value as a newly allocated, previously
unused resource. Its conservative producer execution scope is retained and
completed even when previous contents are discarded.

The existing image-copy path assumes distinct source and destination images in
`GENERAL` and nonempty valid copy regions. Reviewed call paths copy an existing
image into a newly inserted image or synchronize strictly older aliases; the
generic cache copy function does not itself enforce distinct image IDs. The recorder
does not add support for self-copy, overlapping copies or empty-region requests;
those require separate layout and overlap policies. No new early return, layout
tracking, ownership transfer or resource-lifetime policy is introduced here.

## Recording tests and remaining validation

The actual command-recording bodies are extracted into internal functions in
`vk_texture_cache.cpp`, called at the same runtime scheduler positions. The tests
call those functions through the real Vulkan command wrapper and capture its
dispatch callbacks without a Vulkan device. They do not duplicate a barrier-mask
builder or construct the texture runtime with fake scheduler behavior.

Two `[texture_transfer_sync]` cases cover synchronization2 and legacy dispatch,
initialized and discarded upload contents, color/depth/stencil/combined aspects,
multiple mip and layer ranges, distinct source and destination ranges, command
order, every copy-region field, and producer/consumer resource roles. Combined
depth/stencil tests use separate single-aspect copy regions.

The Windows Release test executable was linked against the unchanged recording
bodies first, then against the dependency correction with the same tests:

| Test phase | Result |
| --- | --- |
| Unchanged recording bodies | Both cases failed: 856 of 3,984 assertions failed |
| Corrected recording bodies | Both cases passed: 3,984 assertions |
| Combined regression filter | 120 cases passed: 51,829,728 assertions |

The first phase preserved each original recording body apart from extraction and
formatting. Failures identified missing producer/consumer stages and accesses;
the unchanged copy arguments, layouts, ranges and command order were also checked.
The final correction includes only the reviewed dependencies plus comment and
whitespace cleanup. The combined run extends the filter in
[the build guide](../ORBITALS_BUILD.md) with `[present_transfer_sync]` and
`[texture_transfer_sync]`. Individual checks can be run with:

```powershell
./build/bin/tests.exe '[texture_transfer_sync]'
```

The existing numbered Build 2 package was preserved; GUI and CLI executables have
not been rebuilt for this correction. A fresh validation-layer run must check
the reported hazards before drawing a runtime conclusion. These dispatch tests
do not establish image contents, lifetime across submissions, a visual-artifact
fix or an FPS gain.
