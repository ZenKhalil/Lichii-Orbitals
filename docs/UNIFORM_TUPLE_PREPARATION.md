# Dormant compute uniform tuple preparation

This document records the Candidate 6 dormant preparation and its tests.
The later default-off exact-two experiment is described separately in
[TWO_UNIFORM_EXPERIMENT.md](TWO_UNIFORM_EXPERIMENT.md); three/four-declaration
and graphics activation remain absent.

This preparation is based on Candidate 5 source
`59f6372b5958e52ffce224567baac6777c07a759`. It does not activate multi-declaration
pipeline compilation, caching or selection. Existing single-declaration uniform
and store families, their supported formats and renderer behavior remain the
active paths. Native preparation checks are recorded in
[C6 correctness results](CANDIDATE6_CORRECTNESS.md). Its later gameplay comparison
did not activate these dormant helpers.

The new helper accepts one to four static count-one uniform-texel declarations
only after checking each descriptor and every normalized buffer-fetch index.
Every declaration must have a fetch. Secondary/computed handles, arrays, sparse
operations, buffer sample/gather/LOD operations and unresolved image operations
remain excluded. Other already-normalized image instructions are left unchanged;
their eligibility for any future store specialization is a separate decision.
The helper does not prove immutable input availability or driver/layout safety.

An ordered key contains only the numeric class of each declaration. Its length
and order are significant. Exact supported formats are checked first. R8_UNORM
is classified as Float only in this dormant policy; the active seven-format
single-uniform policy still rejects it. R8's one-byte stride and normalization,
and R16's two-byte stride/default components, remain properties of the original
host view. No view is replaced or reinterpreted. The six-case synthetic GPU value gate is recorded in the C6 results; it does
not activate this policy in the renderer.

The descriptor queue has a dormant bounded observation facility. It copies at
most four requested existing final texel writes, including the original view,
offset, size and format, and returns them in request order. It rejects duplicate
requested slots, repeated writes to a slot and incomplete observations. Acquire
and Take clear the observation state. A null/substituted view is recorded as it
was actually written; recording it is not proof the view is suitable. The old
single-entry observer remains unchanged. No production caller enables the new
observer or calls the new tuple policy in this preparation.

## Validation scope

Four application cases are added under `[compute_uniform_tuple]`:

- Each descriptor and post-TexturePass index is checked, including invalid later
  entries, missing fetches and the four-entry bound.
- Actual TexturePass/collection, immutable cold/disk replay and SPIR-V emission
  cover Uint/Float, Float/Uint, three Uint, and a four-entry signed/mixed tuple.
  The synthetic producer dies before replay; all fetched components reach
  storage-buffer stores, ordered bindings and carrier types are checked, and an
  ordinary R32_UINT image atomic remains present. This is synthetic IR input,
  not guest decoding, actual Configure, or a driver-value test.
- Keys distinguish all valid ordered class vectors, exact format support,
  unsupported formats and inactive-policy isolation.
- The actual queue observation component preserves exact byte ranges and order
  and rejects duplicate/incomplete/reset observations. A separate WORK-only
  exact-method projection passed 50 checks across both descriptor payload modes;
  it is not a Device/Scheduler or a lifecycle test.

Application compilation, the four linked cases and the exact-method projection
passed in the authorized no-game window. The separate six-case synthetic GPU
gate also passed R8 normalization/default components and ordered mixed fetches.
No prior Candidate 5 result is relabelled as validation of these new cases.

## Limits of the dormant Candidate 6 preparation

Current DispatchCompute holds texture and buffer cache mutexes across Configure.
The final type tuple is available only after existing host binding. Any future
on-demand variant build must own its immutable recipe, compile without retaining
guest/environment/IR references, preserve complete Info/layout equivalence, and
capture the selected result across direct, indirect and deferred dispatches.
The existing timeline/build-completion retirement protocol must be extended and
tested before eviction or bounded admission can be considered safe. A cache cap
cannot justify dispatching a type-incompatible fallback or skipping work.

The later exact-two experiment uses eager construction rather than this proposed
on-demand design; see [its separate scope](TWO_UNIFORM_EXPERIMENT.md). Candidate 6
itself supplied neither runtime lifecycle nor joint uniform/store or graphics
four-declaration activation, and its tests did not establish a gameplay fix.
