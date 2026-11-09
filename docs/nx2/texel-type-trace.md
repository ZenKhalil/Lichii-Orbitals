<!--
SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Vulkan texel numeric-type metadata

This unshipped diagnostic investigates [issue #7](https://github.com/ZenKhalil/Lichii-Orbitals/issues/7).
It does not change shader translation, descriptor formats, synchronization or draw selection.
It has not been compiled, tested or used in a game yet. Preparation starts from the exact
Build 2 documentation commit `9a1f589dfa5762623d1f637550f7c94118e594e0`.

Set `EDEN_NX2_TEXEL_TYPE_TRACE=1` before starting the process. Only the exact string `1`
enables the cached process-wide setting. Unset, `0`, `01` and other values leave it off.
The normal log contains `TexelTypeTrace` records; the diagnostic writes no separate files.

The capture copies descriptor metadata during the existing Configure call. A thread-local
scope connects each expected descriptor payload entry to the existing buffer binding's
view handle, offset, size and already-computed Vulkan format. The batch becomes immutable
before the existing scheduler bind lambda uses it. Reporting occurs there, after the
existing pipeline-build wait, and records the actual `VkPipeline` handle for comparison
with validation messages. No additional GPU command, view creation, driver query, guest
memory read, GPU barrier, semaphore, wait or readback is introduced. Diagnostic atomic
budgets and shared ownership do add CPU synchronization overhead. Logging failures are
caught. A logged Configure is not proof that a later draw/dispatch actually accessed it.

## Bounds and coverage

- Separate process-wide budgets allow 64 predicted mismatches and 8 controls for each
  of uniform and storage texel buffers: at most 144 record attempts altogether.
- At most 16 rows are retained per Configure. A full batch records `row_cap_hits`.
  It does not scan or save unbounded guest data. There is no deduplication: repeated
  bindings consume the budget. A later error can therefore be outside coverage.
- A permit is consumed before allocation; allocation failures and abandoned unsealed
  batches are counters. Early returns, cancelled deferred commands or logging failures
  can consume permits without producing records. Counter snapshots are not atomic
  across all fields, and no final summary is guaranteed.
- Mesh/task pipelines are excluded, including compute emulation with multiple host
  pipelines. Their binding topology is not claimed to be covered by this diagnostic.
- Normal drawing and compute dispatch paths are supported. A deferred compute bind
  reports once even if its bind callable is invoked more than once.
- With the switch off, there is no batch allocation, budget access, formatting or log.
  The added scope/TLS checks and empty shared-pointer lambda capture remain small
  overhead; no zero-overhead or performance equivalence claim has been measured.
- Enabled allocation is bounded by the 144 permits. It is diagnostic overhead and
  can alter timing; do not use that run as the sole performance baseline.

## Reading a record

`pipeline` is the actual Vulkan handle; `usable` reports the existing pipeline validity
check, not successful GPU execution. `set`, `binding`, `element` and `entry` distinguish
binding declarations from descriptor-array elements and packed update entries. Binding
numbers follow the actual DescriptorLayoutBuilder order. A `host_observed=false` row
has no confirmed matching host payload entry; its zero view/offset/format fields are
unknown, not evidence of a null resource.

`stage` uses the host graphics slot (0 vertex, 1 tessellation control, 2 tessellation
evaluation, 3 geometry, 4 fragment) or 5 for compute. `shader` is the existing shader
cache/key hash, not the immutable original-native program hash. `auxiliary_shader` is
the vertex-A hash when a combined vertex stage has two guest programs. `program` is
the current primary guest program address from existing engine registers; it is not
a read of program bytes and not proof of a particular earlier compilation's address.
`native` is the existing native-mode flag. Combined vertex-A address is not recorded.

`source` is 0 constant buffer, 1 combined secondary constant buffer, 2 computed handle.
The constant-buffer fields describe the descriptor source; no constant-buffer values
are read again. `handle_program` identifies existing computed-handle metadata, not a
new evaluation. `requested_tic` is the already-resolved TexturePair's image index;
`resolved_tic` applies the same pure TextureHeaderIndex rule using current engine
limit/native-tiling flags. No descriptor is reread to verify its contents. `guest_format`
is the current cached ImageView format, not a new read of the original TIC bytes.

`compiled` mirrors the current SPIR-V emitter's selected sampled type: uniform buffers
are FLOAT, storage buffers use `is_integer` to choose UINT or FLOAT. This is source
metadata, not SPIR-V reflection. `compiled_format` is the storage ImageFormat enum;
4294967295 means no corresponding field for uniform buffers. `selected_format` is the
PixelFormat passed to the buffer cache; `vk_format` is the existing binding call's
Vulkan enum. `mismatch_prediction` compares numeric classes of the emitter and selected
PixelFormat; VVL remains the independent check of actual SPIR-V versus Vulkan format.
Invalid PixelFormat is UNKNOWN and consumes a control permit, not a mismatch permit.

`view` is the existing VkBufferView, also captured in descriptor-buffer mode (where
that handle is not the descriptor-buffer payload). `bind_offset` and `bind_size` are
arguments supplied to BindTextureBuffer, not values queried from the driver. Null
fallback Buffer::View can normalize them internally; do not interpret such a row as
proof of the final null-view range. `guest_address` and `guest_size` describe the
current cached view. These are addresses/ranges, not captured guest bytes.

`read`/`written` are descriptor declarations. `image_atomic_stage` is only the
shader-wide uses_atomic_image_u32 flag. Per-descriptor atomic use remains explicitly
unknown; the flag cannot establish that this descriptor executes an atomic operation.

## Validation gates

Four prepared `[texel_type_trace]` cases exercise the actual header and capture adapter:
strict parser/numeric classes/independent quotas, row bounds/nesting/queue identity/seal
and exception cleanup, concurrent quota grants, and actual Shader::Info declaration vs
payload indexing plus storage/uniform/computed-handle metadata. They are currently
unrun. They do not prove compiled production integration or GPU execution.

Before a diagnostic package: independently review the full diff; compile its production
translation units and tests; run focused cases and fresh-process exact-env checks;
confirm no duplicate View/AddTexelBuffer calls or changed command ordering. Then use a
bounded game run with VVL and correlate exact pipeline/view handles. Private logs can
contain guest and host addresses; keep them local or redact them before publishing.
No shader dumps, game code, texture data or buffer bytes are generated by this trace.
