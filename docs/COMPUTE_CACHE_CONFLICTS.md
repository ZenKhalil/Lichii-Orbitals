# Refuse known-conflicting compute cache inputs

This correction addresses [issue 27](https://github.com/ZenKhalil/Lichii-Orbitals/issues/27).
The integrated regression has now passed baseline RED and guarded GREEN; see
[C6 correctness results](CANDIDATE6_CORRECTNESS.md). Candidate 5 and its immutable
package remain unchanged.

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

## Completed regression

The new regression uses actual `SerializePipeline` and `LoadPipelines`, with a
synthetic input producer. It starts with a valid record, attempts each of the
four conflicting appends, and requires unchanged file bytes, exactly one loaded
record, and preserved positive and negative pointer answers. Stable-repeat and
unbound-instruction controls are retained.

The unchanged serializer ran first with only the regression added: one case,
70 assertions, 12 failures. Each conflicting append remained serializable,
changed the file and loaded an unwanted second record. With the one-line guard,
all 62 assertions passed; the RED callbacks account for the different total.
The subsequent [C6 validation](CANDIDATE6_CORRECTNESS.md) also passed the replay
and compiler/cache selections. No existing cache was deleted or repaired.

This is a local correction to an existing capture invariant, not an upstream
port. Runtime performance and old-cache recovery are outside its claim.
