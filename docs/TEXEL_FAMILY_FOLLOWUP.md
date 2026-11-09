# Texel-family follow-up

This follow-up retains the prepared guest view formats and descriptor payloads.
It does not claim to resolve all numeric mismatches, visual artifacts or frame-rate limits.

## Secondary uniform handles

The narrow VertexB family now rejects descriptors with a secondary handle, matching
its supported single static handle contract. The unsupported path retains the
original renderer behavior. The actual policy regression failed one assertion
before the guard and passed all 22 assertions afterward.

## Mixed compute stores

One static count-one typeless buffer may now coexist with explicitly classified
non-buffer R32_UINT image atomics. The buffer must remain write-only: reads,
atomics, coherent or byte-addressed accesses on that target are excluded. Buffer
and ordinary-image indices are classified in separate namespaces. Unknown image
operations, incompatible metadata, extra sampled/uniform resources and unsupported
atomic increment/decrement remain excluded. Other image declarations and atomic
operations are emitted unchanged through fresh immutable replays with complete
Info and layout compatibility checks.

Three actual TexturePass/replay/emission cases failed four assertions under the
old predicate, then passed 14,333 assertions with the extension. An isolated
actual-emitter Vulkan fixture passed 18 combinations of typed stores and separate
image IAdd, signed-minimum and bitwise-AND operations, including values and canaries,
with zero Core+Sync validation errors. RGBA32 integer views in this fixture are
additional emitter checks, not an expansion of the production store format policy.
The synthetic programs do not establish eligibility of a particular game shader.

## R16 unsigned uniform views

Compute uniform families additionally recognize feature-supported R16_UINT views.
The already-prepared view keeps its offset, range and two-byte texel stride; the
unsigned member returns a U32 component through the existing raw register carrier.
An explicit format-to-class table preserves the original six mappings and maps
R16_UINT to Uint. R16 signed, float and multi-component formats remain unsupported.
The graphics uniform format policy is unchanged.

The actual-emitter test checks the selected U32 fetch and unchanged unrelated
image atomic. A separate headless Vulkan case used a nonzero aligned offset,
a 12-byte view and distinct adjacent halfwords. Exact zero-extension, missing
integer components 0/0/1, immutable input and full allocation canaries passed,
with zero Core+Sync validation errors. A 142-check actual-method queue projection
covered both payload modes and value-captured R16/other-format ordering; it is not
an execution of the renderer's Device, Configure or retirement machinery.

Vulkan references: [uniform texel descriptors](https://docs.vulkan.org/spec/latest/chapters/descriptors.html),
[R16 format definition](https://docs.vulkan.org/spec/latest/chapters/formats.html),
and [image-read conversion](https://docs.vulkan.org/spec/latest/chapters/images.html).
