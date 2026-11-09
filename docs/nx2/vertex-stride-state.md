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

The production change compiled in the integrated candidates. Candidate 2's fresh
Core/Synchronization run reached gameplay and movement with zero indexed and
non-indexed stride-state reports. See the
[runtime record](vulkan-validation-findings.md#build-3-candidate-2). This does not
exhaust every device/settings combination. No mirrored conditional-only test
was added, and no FPS gain or split-screen artifact fix is claimed.
