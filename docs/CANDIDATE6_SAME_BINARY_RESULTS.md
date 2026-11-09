# Candidate 6: same-binary PAUSE comparison

Candidate 6 remains an unreleased experiment and is not promoted. One PAUSE remains the default; these measurements do not establish steady 30 FPS or resolve rendering artifacts.

Source: `876b7e3082ebf9d93118df6bdd1fad02615e2a1e`. All three fresh processes used the same executable, SHA-256 `1de11522af1cbeda828c2dca9f94a5e5dad3d9d62b2236a8c1e878a50dc57318`. The only experimental child setting changed was the exact startup value `EDEN_NX2_EXCLUSIVE_PAUSE2=1` in On B; both Off runs left it unset. WAITPKG stayed disabled. The title/configuration pins and NVIDIA application preference were retained. No Vulkan validation layer, texel trace or heavy GPU profiling was enabled.

| Run | PAUSEs per retry | Window | FPS | Mean ms | p99 ms |
|---|---:|---:|---:|---:|---:|
| Off A | 1 | 30 s | 25.02951 | 39.953 | 50.565 |
| Off A | 1 | 20 s | 24.28218 | 41.182 | 52.345 |
| On B | 2 | 30 s | 22.71088 | 44.032 | 55.325 |
| On B | 2 | 20 s | 23.72756 | 42.145 | 51.675 |
| Off A2 | 1 | 30 s | 24.16877 | 41.376 | 52.319 |
| Off A2 | 1 | 20 s | 25.24450 | 39.613 | 49.878 |

Graphics/memory clock snapshots were 1710/9501 MHz before and after all six intervals. Both players remained at the initial room positions for these measurements; no camera or movement input was issued during them.

This was a short, ordered off/on/off comparison, not a randomized causal benchmark. The first Off A measurement included an entrance transition and is excluded. Its extra exploratory measurements also delayed the accepted Off A pair relative to room entry; all six accepted intervals used the settled initial-room view, but time since entry was not matched. Opening cinematics were skipped. Warm-cache history, process order and ordinary run-to-run variation remain limitations. GPU clock readings bracketed each interval; they are snapshots, not continuous telemetry.

Saved code independently confirmed the intended 1/2/1 sequence. The verifier checked the executable hash, process start identity, contiguous instruction boundaries, original retry targets and a matching inline-read reservation prefix for the monitor pointer. The result establishes the selected emitted path at observed saved sites. Counts are unique code sites, not executed acquisitions, CPU time or complete JIT coverage. Raw sampling and bounded code capture occurred after the frame measurements.

Observed code coverage: Off A: 743 unique 1-PAUSE acquisition sites in 4271 stable saved windows; On B: 727 unique 2-PAUSE acquisition sites in 3892 stable saved windows; Off A2: 727 unique 1-PAUSE acquisition sites in 3758 stable saved windows.

All six enclosing audio brackets had copied=accepted and zero full/retry increments or Error/Critical rows. 5 of six had zero padding/deferred increments. Exceptions: Off A2 30 s: +496 padded frames (10.333 ms), +3 deferred. Each complete run contained the same 135 known startup error records, with no pre-close new error, new shader rejection or host-audio fault. Last logged full-run padding totals were Off A: 11,760 padded frames (245.000 ms); On B: 12,656 padded frames (263.667 ms); Off A2: 15,200 padded frames (316.667 ms). These totals precede close and include loading/other phases; they do not establish audible synchronization.

All three processes exited after UI close without an OS kill. Off A and On B had no after-close errors. Off A2 logged four guest panic/break/backtrace errors after its close request (panic code 1A80A), then completed cleanup and frontend close. This is not an error-free shutdown claim. A2 also had a separate pause-menu/window-occlusion and Alt+F4 close phase after all measurements and code capture; no causal attribution is made.

Faint bands remained visible in the inspected initial-room views. The comparison did not reproduce every earlier machinery-glare viewpoint and does not establish artifact resolution. Candidate 6's multi-uniform preparation remains dormant: source guards and synthetic values do not mean those game shaders were selected or fixed in these runs.

Separately, the user subsequently reported that the left-side issue was not visible in the latest Candidate 6 check. This is a point-in-time user observation. The previously intermittent issue is not proven fixed.
