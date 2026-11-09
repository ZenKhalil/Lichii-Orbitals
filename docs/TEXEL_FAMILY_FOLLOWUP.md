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
