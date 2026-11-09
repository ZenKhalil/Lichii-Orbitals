# Targeted texel metadata trace

This optional diagnostic narrows metadata coverage to selected existing shader
hashes. It does not change rendering, shader specialization, descriptor views or
commands. It is separate from the unmodified Candidate 5 package.

The existing exact `EDEN_NX2_TEXEL_TYPE_TRACE=1` switch must enable tracing first.
At process startup, the optional `EDEN_NX2_TEXEL_TYPE_TRACE_SHADERS` value selects
one to eight unique hashes: exactly 16 hexadecimal digits each, separated by
commas, without spaces or `0x` prefixes. Uppercase and lowercase digits are
accepted. The hash must match the trace's `shader` field, not `auxiliary_shader`,
the guest address or a pipeline key.

An absent filter preserves the previous trace behavior. An empty or malformed
present filter rejects every metadata row; it never falls back to broad tracing.
The immutable parser examines at most 136 bytes and allocates no memory. Its
result is cached, so a process restart is required to change it. A caller that
launches a diagnostic process must validate and record the exact child-only
value; this patch does not alter any launcher or package configuration.

Filtering occurs before batch-cap accounting, assessment copying and budget
reservation. Excluded rows consume no ticket or batch slot. Accepted rows keep
the original prepared-view metadata and include `shader_filter_hashes`, the
number of configured hashes (zero when unrestricted). An invalid filter cannot
report per-row status because it emits no rows; preflight validation is needed
to distinguish invalid input from absent coverage.

The original 64-mismatch/eight-control limits per stage family and descriptor
kind and 16-row Configure limit remain unchanged. Several selected shaders still
share those limits; one can exhaust them before another. Use one selected hash
per fresh process when a particular positive control is required. Missing rows
remain unknown. Filtered metadata omits unrelated shaders, and validation-layer
duplicate limits still apply.

Two prepared actual-header cases cover strict parser boundaries and the original
coverage failure: unrelated early controls exhaust the quota before a later
target. The second case uses actual Capture/Scope methods, tests target tickets,
host metadata, disabled/invalid states, unchanged unrestricted behavior and
filtered rows after a full batch. The RED version omits only the membership gate.
The integrated seven-case suite and fresh-process selector checks have passed;
see [C6 correctness results](../CANDIDATE6_CORRECTNESS.md). These include the
existing budget, lifetime and scope cases.

No test or trace proves pixel correctness, game-artifact causality, a particular
draw's execution, or that every selected shader received coverage. This feature
is a bounded observation aid, not a rendering fix.
