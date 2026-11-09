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

## Bounded family assessment

The existing exact `EDEN_NX2_TEXEL_TYPE_TRACE=1` opt-in adds the first family
exclusion reason, assessed stage, declaration/target count, offending opcode when
applicable, feature mask, snapshot and construction status to its existing bounded
Configure rows. The pipeline owns the assessment; each queued immutable batch
contains value copies, not a pointer back to compiler or pipeline state.

The assessed stage is distinct from the descriptor row stage: 0 denotes the
emitted graphics vertex slot, 5 compute. Local policy eligibility can be true even
when an outer construction, feature or snapshot gate prevents a family. An
unobserved assessment is unknown. First reason is not an exhaustive inventory.
Existing control/mismatch quotas and row limits remain; absent later records do
not establish compatibility. Store families may carry unknown uniform assessment.
No new guest reads, format queries, GPU commands or waits are added by this
metadata. Disabled diagnostics still have small object/Scope field overhead.

Five trace cases passed with the environment both absent and enabled; tests cover
immutable row copies, stage identity, disabled/unknown transport and existing
bounded budgets. They do not execute the real logger or driver construction path.

## Streamline requirements trace

`EDEN_NX2_STREAMLINE_REQUIREMENTS=1` logs already-returned DLSS Vulkan requirements
at startup, without changing feature negotiation or making additional SDK/device
calls. It is disabled by default, limited to four records per process, 16 names
per list and 96 bytes per name, and catches logging/allocation exceptions. Normal
compilation and a later explicit-flag startup observation are its validation gates;
no extra standalone test harness was introduced.

## Combined validation and remaining scope

The integrated source passed 161 regression cases and 95 compiler/cache cases,
including the existing replay, typed-store and uniform-emitter coverage. The
recorded pre-package run had 51,911,387 and 448,211 assertions respectively;
contention assertion totals can vary. No elapsed-time threshold is a performance
claim. All GPU fixtures are synthetic compute programs. Actual graphics draws,
application Configure selection, failure-injected construction and queued
retirement still require appropriately scoped runtime evidence. A fresh bounded
application VVL run must correlate the actual pipeline/view/descriptor; category
caps and absent records are coverage limits, not proof of no remaining errors.
