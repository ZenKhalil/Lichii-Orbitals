# Vertex binding stride declaration

This candidate addresses [issue 8](https://github.com/ZenKhalil/Lichii-Orbitals/issues/8).
Build 2 triggered both indexed and non-indexed draw validation errors because
`vkCmdBindVertexBuffers2` supplied strides after binding a pipeline that did not
declare the stride dynamic state. Full dynamic vertex input was declared instead.

The [Vulkan command specification](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdSetVertexInputEXT.html)
permits that alternate stride setter when the pipeline also declares
`VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE`. The captured validation errors
therefore identify a command/declaration mismatch, not a proven pixel defect.

The candidate keeps the stride declaration whenever extended dynamic state is
enabled, including pipelines that also declare full dynamic vertex input. It
does not change buffer handles, offsets, sizes, stride values, repacking or
command order. Mesh pipelines still remove both vertex-input dynamic states.
The maximum declaration count becomes 34, within the existing fixed capacity.

Both pipeline feature selection and buffer binding read the device's enabled
extended-dynamic-state policy, including the user's setting. Full dynamic vertex
input has its own setting. Validation must cover their combinations; it must not
substitute a device-support check for the actual enabled policy.

This change is not yet compiled or runtime-validated. The existing linked
regressions and a fresh application run with core validation are required.
The latter must exercise both indexed and non-indexed draws and record zero
stride-state errors in a fresh capture with no truncation. Normal visual/audio
checks follow; reaching a duplicate-message limit never counts as a clean run.
A test that only repeats the declaration conditional would not validate command
compatibility, so no such test is added. No FPS gain or fix for the reported
left-view artifacts is claimed. Build 2 remains the published recommendation.
