# Candidate 6 correctness preparation

This unreleased branch starts from Candidate 5
`59f6372b5958e52ffce224567baac6777c07a759`. It adds the known-conflict cache
serialization guard, an optional two-PAUSE exclusive-monitor experiment, a
shader filter for the existing opt-in texel trace, and dormant uniform-tuple
helpers. It does not activate multi-declaration pipeline selection or change
the recommended Build 2 package. No gameplay or FPS improvement is claimed.

## Completed native checks

The application test target was compiled with the original CMake dependency
module; the prior candidate's local checksum override was not needed. Only
the test target was built. The following results apply to the integrated source
at `44fcb587ac424716eda1c49fa099a8ddec259b5a`:

- The serialization regression first ran with only its test added to the
  Candidate 5 implementation: one case, 70 assertions, 12 failures. Each of the
  four conflicting-input controls remained serializable, changed the existing
  file and loaded an unwanted second record. With the guard, all 62 assertions
  passed. Different assertion counts reflect the extra record callbacks in RED.
- The four actual linked PAUSE2 cases passed 294 assertions in each of two fresh
  processes, with WAITPKG disabled and PAUSE2 respectively `0` and `1`. These
  include the real A64 JIT same-value-STXR reservation checks through callbacks
  and aligned writable fastmem. They use sequentially interleaved JITs, not
  simultaneous guest execution or complete Core/GPU fault handling.
- Four dormant tuple cases passed 887,075 assertions; ten compute replay cases
  passed 246; seven texel trace cases passed 1,303 with tracing enabled.
- The combined selected regression filter passed 172 cases and 52,797,297
  assertions. The separate shader/compiler filter passed 96 cases and 448,273
  assertions. The combined filter includes tracing disabled and the existing
  WAITPKG and ordinary-caller controls. Contention assertion totals can vary.
- The WORK-only queue projection passed 50 checks using actual extracted inline
  methods and the integrated policy. It covers ordered observations in both
  payload modes, but does not instantiate a Device, Scheduler or Configure path.
- The actual-header shader-filter fixture passed 282 checks across 12 fresh
  processes, covering absent/disabled/enabled tracing, filter parsing, cached
  choices and lazy lookup. No parent environment was changed.

No production compile or linked-test correction was needed. The separate tuple
SPIR-V exporter initially failed to link because its borrowed library list
omitted the test target's existing VMA implementation object. A retry pinned and
reused that object; it changed no production source. Four modules and five small
synthetic input fixtures were produced from actual immutable replay and emission.
The first failed link and successful retry were retained separately.

## Driver values and remaining limits

Six headless GPU cases passed with the actual exported modules and the pinned
validation layer, with Core and Synchronization Validation enabled and zero
validation errors. They cover Uint/Float and reversed Float/Uint declarations,
three Uint declarations, and a signed/mixed tuple. R8_UNORM values and missing
components, R16 zero extension, finite float and raw integer carriers, full input
and padding preservation, and the ordinary image IAdd result all passed. The
R8 nonendpoint oracle allowed one ULP; observed values matched the nearest F32
exactly. Six loader warnings were retained separately from validation errors.

These sequential synthetic values do not establish renderer selection, queued
lifetime, retirement or graphics correctness. The production single-declaration family remains the
active path. No multi-declaration fix is implied by successful preparation.

PAUSE2 remains off by default and requires a fresh process to change its cached
option. After a final experimental binary is reviewed, compare that same binary
with PAUSE2 off/on/off at the same pose, settings and power state. Preserve the
serialization guard and all other source/configuration in both arms, verify the
actual JIT wait-loop selection, and report frame-time tails as well as averages.
Short lock-test timings are correctness diagnostics, not performance evidence.

At the test-stage revision recorded here, no final GUI/CLI package or gameplay
run had yet been completed. The later Candidate 6 package and off/on/off results
are recorded separately in [same-binary results](CANDIDATE6_SAME_BINARY_RESULTS.md).
This document retains the original test-stage counts and dormant policy scope;
Candidate 5's immutable package remains preserved.
