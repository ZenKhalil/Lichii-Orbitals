# Refuse known-conflicting compute cache inputs

This prepared change addresses [issue 27](https://github.com/ZenKhalil/Lichii-Orbitals/issues/27).
It has not yet been compiled or tested in the application. Candidate 5 and its
immutable package remain unchanged.

Compute input capture already detects repeated queries that return different
constant-buffer values, texture types or texture formats, and forced pointer
classification changes after capture starts. Live query results are preserved,
but an immutable replay cannot represent both answers. Replay therefore rejects
that capture. The disk serializer previously accepted the same environment as
long as its instructions were bound.

`GenericEnvironment::CanBeSerialized()` now also rejects the existing conflict
flag. No query, rendering, cache loading, record order or on-disk version changes.
Code mutations that invalidate an armed capture also set this flag and are
conservatively excluded. Direct low-level `Serialize()` is unchanged; the
pipeline append routine owns the eligibility check.

This does not repair, remove or reinterpret existing records. In particular, it
is not a demonstrated repair of the older incomplete pointer-classification
records discussed in issue 25. A rejected append leaves an existing valid cache
unchanged; opening a new file can still create its ordinary header before the
eligibility check.

## Required validation

The new regression uses actual `SerializePipeline` and `LoadPipelines`, with a
synthetic input producer. It starts with a valid record, attempts each of the
four conflicting appends, and requires unchanged file bytes, exactly one loaded
record, and preserved positive and negative pointer answers. Stable-repeat and
unbound-instruction controls are retained.

After an explicit no-game native window, run the new regression first against
the baseline implementation with only the test added. Preserve the expected RED
failure. Then enable the one-line guard and require GREEN, followed by the
compute replay and selected pipeline-cache/compiler regressions. No baseline
failure or passing linked result is claimed by this source-preparation commit.

This is a local correction to an existing capture invariant, not an upstream
port. Runtime performance and old-cache recovery are outside its claim.
