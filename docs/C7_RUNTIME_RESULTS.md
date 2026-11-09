# Candidate 7 runtime results

Candidate 7 uses source `028ea2427dc3af0dc76bccc32bbf0f3c21609482` and the same executable for the checks below. It remains an unreleased candidate. The optional exact-two uniform specialization stays **disabled** for the planned numbered release: its binding check succeeded for the eligible target, but its enabled validation run failed shutdown. A normal enabled run is deferred. The shutdown cause is unestablished.

## Filtered Vulkan checks

Both runs used the same pinned validation layer with Core Checks and Synchronization positively observed, and a metadata filter selecting only `92533bc272bfe888` and `a11cbb9e43bcccaf`. Validation itself was not filtered to those shaders. Both routes reached the opening after prolonged loading; neither covered the full opening, room or movement. No FPS benchmark was performed under validation.

| Check | Option off | Option on |
|---|---|---|
| Eligible shader `92533…` | FLOAT declaration mismatched RGBA32_UINT; eight retained errors joined to exact pipeline/view/set/binding/element | Two complete observed pairs matched UINT/RGBA32_UINT and FLOAT/R8_UNORM; eligibility and family-built metadata were positive |
| Shader `a11c…` | FLOAT declaration mismatched R16_UINT | Intentionally excluded as `StorageImageDeclarations`; the mismatch persisted |
| Metadata coverage | 64 uniform mismatch rows, eight uniform controls, eight storage controls | Same bounds; eligible target contributed five compatible rows before the shared positive quota was exhausted |
| Global validation output | 41 errors,20 warnings, three status messages | 41 errors,20 warnings, three status messages |
| Normal shutdown | Completed after UI close, without an OS kill | Failed after UI close; required guarded OS termination |

The second target also uses storage texel buffers, so two uniform declarations alone do not make it eligible. Its exclusion is a negative control, not a failed activation target. The first target's on-run controls were retained in an early snapshot while the dispatch-error category still contained 12 messages, below its 20-message cap. No exact error joined to its observed on-run pipeline/view pairs. Later caps prevent broader absence claims. The aggregate recorded one completed family construction, nine assembled members and 624,995 microseconds of cumulative construction time; this is not a loading-duration or performance measurement.

Both final logs reached the 20-message caps for dispatch and indexed-draw texel errors and the layer-count warning. A private-data feature error also remained. No synchronization-hazard or vertex-stride category was captured on these routes. This does not establish universal hazard absence, correct pixel values, all uses of a shader or a fix for intermittent glare/banding.

All stdout and stderr bytes were retained through EOF without truncation. The enabled run initially failed module enumeration just after launch, then verified the exact layer path/hash about two seconds later; its retained supervisor warnings include that inspection failure and a process-path read failure around termination. Positive activation and complete capture are distinct from a successful shutdown.

## Audio and shutdown

Both runs logged the 135 established emulator errors before close, with no new native graphics rejection or host audio-device fault. Last logged audio totals precede shutdown:

| Check | Option off | Option on |
|---|---:|---:|
| Copied / accepted frames | 17,545,920 / 17,545,440 | 17,651,328 / 17,637,888 |
| Padded frames / duration | 57,840 / 1.205 s | 52,752 / 1.099 s |
| Full-queue events / retried frames | 2 / 480 | 56 / 13,440 |

These counters do not establish audible synchronization or dropped unique frames. Off logged no new error after close and completed cleanup. On requested close at 403.891s, invoked ForceStop at 404.891s and immediately logged guest panic 1472 with three related entries. Cubeb stream teardown, the family aggregate and Streamline stop progressed, but the frontend reported its emulation thread failed to stop at 414.891s. Cleanup did not finish; the process remained unresponsive and was terminated after identity verification. Five Error/Critical entries occurred after close. There was no preceding logged endpoint-loss chain. Earlier runs also had shutdown panics, sometimes followed by successful cleanup; this does not prove the new family caused this hang.

The title configuration remained unchanged. The enabled run left the already-known global shortcut/default variant; a key comparison found only shortcut/default entries, and the reviewed baseline was restored before the next run.

## Normal option-off check

The same executable completed the full opening without skips, two stationary initial-room measurements, then camera movement and walking toward the machinery. The experiment, validation layer and texel trace were off. The process exited normally after the UI close request, without an OS kill; launch to the coordinator's exit observation was about 11 minutes 28 seconds. The title and global configuration hashes matched their starting values.

| Normal scope | Duration | Frames | FPS | p99 frame time |
|---|---:|---:|---:|---:|
| Opening-only window | 30s | 887 | 29.588 | 60.438ms |
| Later opening-only window | 30s | 883 | 29.445 | 66.801ms |
| Stationary initial room | 30s | 727 | 24.279 | 50.394ms |
| Same room repeat | 20s | 499 | 24.996 | 46.976ms |

All four wider audio-log brackets had no added padding, deferrals, full-queue events, retries or Error/Critical entries. Across the run, the last logged counter admitted all 31,173,664 copied frames, including 30,067,660 nonzero frames, and accumulated 11,936 padded frames (**248.667 ms**), 60 deferrals and no full-queue event or retry. These last totals precede close; neither they nor the brackets prove audible A/V synchronization.

The log contained the same 135 known errors and no new native graphics rejection, host audio fault or guest panic. First presentation at 15.160 s was followed by stereo Open/Attach at 32.930/32.932 s; presentation is not gameplay readiness, and menu delays prevent a launch-to-gameplay timing claim. Close requested at 684.095 s invoked ForceStop at 685.093 s; cleanup and frontend close completed at 686.409/686.421 s. No Error/Critical entry followed the close request.

Initial views were intact. Later movement brought Player 1 near machinery and a nearby broad blue glow/flare appeared on the left while the right camera looked elsewhere. This was not an identical-camera asymmetry comparison and does not establish that all observed lighting effects are unintended. It does not prove the earlier intermittent visual issue is absent or fixed.

This normal sample uses different instrumentation and broader route coverage than the two validation runs. It is not an off/on FPS comparison and does not establish the cause of the enabled run's shutdown hang. The second PAUSE and exact-two uniform experiment remain off. The planned numbered release still requires its own exact-binary smoke after the final documentation/build identity is set; the normal candidate result does not replace that gate. No stable 30 FPS or visual-cure claim is made.
