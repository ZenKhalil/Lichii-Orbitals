# Opt-in two-uniform compute experiment

This experimental source starts from Candidate 6
`876b7e3082ebf9d93118df6bdd1fad02615e2a1e`. It is not a recommended release and
does not establish a fix for reported flicker, loading time or frame rate.

## Policy and construction

Only the exact startup value `EDEN_NX2_MULTI_UNIFORM2=1` enables the new path.
The cached choice requires a fresh process. Absent, empty, `0`, padded or other
values leave the existing single-declaration family and excluded-program paths
in place. The opt-in path requires exactly two static, count-one uniform texel
declarations, valid normalized fetch indices and no secondary/computed handles.
Storage texel declarations are excluded. Ordinary images and their operations
are retained, including unrelated atomics, through complete immutable replay.
Three/four uniform declarations and graphics stages remain excluded.

Before cache publication, fresh replay constructs all nine ordered Float/Uint/Sint
pairs. Each translation must preserve the entire original Info, workgroup and
memory metadata; all emitted Info values must agree. Every module exists before
any synchronous pipeline object is constructed. All members must be built,
nonnull and layout-compatible before the all-Float base takes ownership of the
other eight. Partial construction or validation failure destroys local members.
The path does not introduce a deferred worker build or a scheduler error channel.

After the existing host buffer binding, Configure consumes the two observed
final descriptor writes in declaration order. The format, offset, size and view
are copied from those writes; no extra view creation, guest read or format
reinterpretation is performed. Unsupported or missing final tuples fail
explicitly in this opt-in experiment instead of choosing a mismatched Float
member. Numeric class selection supports the original R32/RGBA32 Float/Uint/Sint
formats plus R16_UINT and R8_UNORM where the device reports uniform-texel support.
The latter formats retain their two-byte and one-byte texel strides.

The selected pointer is returned through the existing Configure interface and
captured by the unchanged binding, direct, indirect and deferred commands. The
base retains all children; its existing timeline retirement also checks their
build completion. This source argument is supplemented by exact-method tests;
it is not a claim that a projection exercises the full Device or Scheduler.

## Cost and measurement

The eager path creates nine pipeline objects per eligible key, eight more than
the prior excluded one-pipeline path. It also repeats descriptor bookkeeping in
the synchronous child constructors. A fixed per-family count does not bound
total driver memory across all cached keys. Default-off pipelines carry the
new empty ownership fields, and completion checks traverse the empty child
array; the disabled path does not replay or build additional members.

When both this experiment and the existing exact-one texel trace are enabled,
each pipeline cache emits one aggregate at destruction: attempted families,
completed families, assembled members and total build microseconds. Completed
means family assembly returned successfully, before outer cache insertion. The time
includes failed attempts and host format/capture/construction work. Concurrent
attempt durations are summed, not reported as elapsed startup wall time. These
counters contain no shader payload. Missing final output after forced termination
does not establish zero work.

Use the same binary with the new flag off/on/off and keep other settings fixed.
Measure cold/warm cache conditions, eligible-family counts, startup and first-use
stalls separately. Trace runs establish selection and validation evidence;
clean frame-time measurements should keep heavy diagnostics off.

## Prepared validation and remaining gates

Two added application cases cover the exact gate and ordered class indices,
all supported format pairs, negative controls, and all nine owned cold/warm
replays with exact SSBO carrier offsets and preserved ordinary-image atomic
resource/type/value chains. Existing Candidate 6 six-case GPU values validate
synthetic tuple emission, including R8 normalization, but do not validate this
new runtime selection path.

A separate exact-method projection covers family rejection and cleanup,
construction/null/build-pending failure inputs, complete versus missing final
observations, format order and offsets in both descriptor payload modes,
queued A-to-B pointer capture, and parent ownership across a projected completion
timeline. It uses actual changed methods with projected handles and constructor
results. It does not instantiate a Vulkan Device, execute full Configure or
prove real scheduler retirement. Source guards additionally check the unchanged
direct/indirect/deferred capture sites.

Its portable preparation script and fixture are under
`tools/testing/uniform2_projection`. Run `prepare.py --source <source-root>
--output <new-output-directory>` to extract guarded actual methods and record
their source hashes. Compile the generated `projection.cpp` with the application's
test compiler, dependency includes and library set; this is a separate small
executable, not an emulator invocation. With no argument it runs the ownership
and observation projection. An argument `0` or `1` tests the cached startup flag
against that expected Boolean, changes only its own child environment, and
checks that the cached choice remains stable. Use fresh processes for absent,
empty, padded, alternative and exact-one values. The preparation script itself
does not configure, compile or launch anything.

No new native, driver or gameplay test has run for this source draft. Before
promotion, run the linked cases and fresh-process cached selector controls,
the exact-method projection, and the combined regression suite. Then use the
actual candidate with Core/Synchronization Validation and final-view diagnostics
to establish that the intended two-declaration programs select compatible
members. Check shutdown, cache replacement and both dispatch forms with the
stated limits. Failure cleanup is not a promise of graceful application shutdown
for every allocation or driver failure.

The upstream reference checked for the original uniform declaration behavior is
`zortos293/Lichii` commit `cbb512ee7a9de9d9dbe2f865ec2e468888503214`.
Its uniform texel declarations use Float. This local experiment is not an
upstream port and does not alter the upstream eligibility policy implicitly.
