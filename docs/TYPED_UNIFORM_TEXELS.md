# Typed uniform texel-buffer fetches

Vulkan requires a shader's sampled numeric class to match its texel-buffer view.
The inherited uniform-buffer emitter always declared float images, including
when the prepared view was an unsigned integer format. The general emitter now
accepts a numeric class for each declaration. An empty specialization vector
retains the original float path.

Integer fetch results are bitcast into the existing float-vector carrier before
the ordinary guest-register bitcast. They are not numerically converted. The
view format, component count, texel stride and descriptor are preserved. Buffer
dimension queries retain their unsigned integer result. Specialization rejects
an incomplete vector or unsupported descriptor arrays.

## Native vertex family

The initial graphics path is limited to one static, count-one uniform texel
declaration in a native VertexB program. It requires a plain buffer fetch and
allows its dimension query. VertexA/dual programs, task/mesh and generated
geometry, sparse operations, other texture/image operations, descriptor arrays
and computed handles are excluded. Other stages must have no uniform texel
declarations. Only supported R32 float, unsigned and signed views select members.

A bounded recording environment forwards the original compiler answers and
retains owned inputs. Changing answers, excess queries or a missing replay query
prevent specialization. Eligible programs share one immutable snapshot between
fresh compiler instances; their transient caches remain separate. The affected
stage is retranslated with the captured profile, options, linkage and bindings.
Other stages reuse their already emitted code through separately owned modules.
No disk-cache format changes are made.

## Native compute family

The compute path independently requires one static, count-one uniform texel
declaration and a buffer fetch. It excludes secondary/computed handles, arrays,
multiple declarations, sparse operations and buffer-target sampling operations.
Other images and their atomics retain their instructions and prepared resources.
The existing storage-write family's eligibility is unchanged.

Cold and loaded-disk compute inputs use the immutable replay API. Fresh compiler
instances capture the same profile and options, including the existing
Android-only bloom option; other callers retain their previous defaults. Supported
R32 and RGBA32 float, unsigned and signed views select the corresponding member.
The full pre/post-emission shader metadata must match. That comparison is a
compatibility check, not a proof of arbitrary instruction equivalence.

## Selection, ownership and limits

Each family creates three members synchronously before publishing the cache
entry. Modules are prepared before pipeline objects; notifications have scoped
completion, and unbound or incompatible members are rejected. The float base
owns its siblings through the existing retirement lifetime. Descriptor order,
layout and binding modes must agree.

Selection uses the exact host format already passed to the descriptor queue
after view selection and null substitution. It adds no guest descriptor read,
view reinterpretation or readback. Queued binding captures the selected member;
graphics scheduler identity and compute direct/indirect readiness checks use
that same member. The base retains compatible resource bookkeeping.

Unavailable snapshots and unsupported or unadvertised formats retain the base
path and may retain existing mismatches. Other image/store mismatches are outside
this change. Replay/member incompatibility fails compilation rather than exposing
an incomplete family. Three eager pipelines can increase startup time and memory.
No visual correction or frame-rate improvement is established by these tests.
Creation eligibility diagnostics are opt-in and capped; warm-cache creation can
consume that budget before a target shader appears. A missing creation record is
not evidence of eligibility. Actual selected-pipeline/view metadata remains the
runtime check.

## Validation

The integrated source passed four actual-emitter cases, eight graphics
capture/replay/policy cases and three compute-uniform cases, plus the nine
existing compute replay and four storage-family cases. The combined regression
filter passed 155 cases; the compiler/cache filter passed 95 cases.

The graphics fixture decodes and translates a synthetic fetch-bearing VertexB
program and follows bit-preserving dataflow into four storage-buffer components.
It does not exercise native Ampere lowering or a real graphics draw. The compute
fixture uses actual texture and information passes on fresh mixed fetch/atomic IR,
checks cold/warm metadata parity, and separately checks atomic opcode/type chains.

A headless Vulkan compute probe used the actual production emitter for nine
cases: the original float control, six R32/RGBA32 numeric formats, and two separate
special-value observations. Finite/integer values, missing components and input/
canary preservation passed with Core and Synchronization validation enabled and
zero validation errors. Integer missing components remain raw zero/one, distinct
from float zero/one. Arbitrary NaN payload preservation is not a portable claim.
Default and explicit float emission matched within the new emitter; this is not
a saved pre-change byte comparison.

An 82-check projection exercises the actual inline descriptor-queue methods and
both format policies, including unrelated entries, value-captured A-to-B
selections, reset/null/unsupported cases and both descriptor modes. Its backing
storage and callbacks are projected; it does not construct a renderer scheduler.

These checks do not establish actual graphics pipeline execution, application
Configure/scheduler ordering, complete constructor-failure injection or retirement
behavior. A fresh application validation run is required before promotion.
