# Reordered buffer upload synchronization

This correction addresses a source synchronization gap recorded in
[issue #13](https://github.com/ZenKhalil/Lichii-Orbitals/issues/13).
It follows candidate source `f0fe835fd24068c4901945345293239ecc97a048`. No runtime fix,
performance gain or connection to left-view artifacts is established here.

## Source finding

The buffer runtime may move stream-buffer uploads into the upload command buffer
when destination usage tracking permits it. Previously, that branch recorded only a copy,
even when the caller requested a barrier. Multiple uploads can revisit a range
before a later bind marks it used. The scheduler's end-of-upload publication
barrier does not order those earlier copies against one another.

The candidate adds a transfer-write dependency before reordered copies only when
the existing `barrier` argument is true. It keeps upload eligibility, staging and
destination handles, copy regions, scheduling, final publication and the normal
command-buffer path unchanged. The dependency covers copy writes without adding
graphics or compute stages.
It conservatively orders disjoint uploads too; no new overlap tracker is added.

The observed validation report is a buffer copy write-after-write with no write
barriers. Its buffer has not been mapped to this recording path. Adjacent reported
current ranges are not evidence that those ranges overlap each other, and a reused
command-buffer handle does not by itself establish a shared recording generation.
Earlier presentation errors filled the same category limit, so this report must
not be described as a new regression merely because it is now visible.

## Caller contract and limits

The two reviewed reordered-upload callers request barriers. Existing callers
passing `barrier=false` use the normal command-buffer path with explicit batch
pre/post barriers, including downloads and multi-range gathering. The recorder's
false control adds no barriers and does not reinterpret that caller contract.

This change does not repair invalid upload eligibility or an incorrectly tracked
prior shader access. It does not move copies across additional commands, change
usage tracking, or add general graphics synchronization before the first upload.
Overlapping destination regions within one copy command remain subject to the
existing Vulkan validity requirements; the regression uses valid, nonoverlapping
regions within each command and overlap only between commands.

## Linked regression tests

Three `[buffer_upload_sync]` cases call the actual upload recorder through the
real Vulkan wrapper with captured dispatch callbacks. They cover exact/partial
overlap between two copies, disjoint regions, false-barrier controls, sync2 and
legacy dispatch, dependency placement, and unchanged handles/region fields.
The unchanged recorder failed the two barrier-requesting cases and passed the
false control: 30 of 222 assertions failed. The corrected recorder passed all
three cases and 318 assertions. The unchanged recorder was extracted from the
production upload lambda with the same copy arguments before the correction.

The combined regression gate, including the separately reviewed stage-family
trace quota change, passed 127 cases and 51,833,695 assertions. Run the focused cases with
`tests.exe '[buffer_upload_sync]'`. Both tests and production translation units
were built with the existing Release/ThinLTO configuration; these are dispatch
recorder tests, not a Vulkan device integration test.

These recorder checks do not submit GPU work. The later Candidate 2 application
run reached co-op gameplay and movement with zero captured synchronization
hazards; those categories did not reach their message cap. The
[runtime record](vulkan-validation-findings.md#build-3-candidate-2) retains the
coverage limits. The original live buffer was not independently attributed to
this route, and no pixel or FPS improvement is claimed.
