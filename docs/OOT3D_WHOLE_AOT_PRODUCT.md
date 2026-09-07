# OOT3D whole-AOT product runtime

## Objective

Build `code.bin` as a closed, native x64 static recompilation product. The
runtime must not depend on an A32 interpreter, packed-op fallback, source
overlay, or a registry of selected functions. Host boundaries remain explicit
for operating-system services and the three audited atomic helpers.

## Closed surface

The contract is stored in
`tools/oot3d/native_a32_runtime/whole_aot_product_manifest.json` and is audited
on every product consumer link.

The 2026-08-28 portability work changes the generated function ABI and adopts
affinity sharding. Existing Windows archives from the earlier incremental ABI
must be regenerated. The shared changes, current Linux evidence and controlled
Windows A/B procedure are recorded in the
[cross-platform performance status](OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md).

| Item | Count |
| --- | ---: |
| Functions in the source inventory | 12,422 |
| Functions compiled to native x64 | 12,419 |
| Audited atomic host helpers | 3 |
| A32 blocks | 161,341 |
| Unique A32 instructions | 918,019 |
| Dispatcher entries | 161,332 |
| Residual A32 entries | 0 |
| Unclaimed blocks | 0 |
| Unresolved static edges | 0 |

The product runtime rejects packed-op fallback and reports nonzero memory
fault, unsupported-exit, block-limit, or retained-A32 counters as validation
failures.

The NRI/Vulkan consumer also builds a default PICA shader pack offline from a
versioned effective-state inventory. Boot/open-title, the Kokiri cutscene and
deterministic Kokiri gameplay run in strict mode with 91 precompiled modules
and zero dynamic shader misses. Architecture, qualification and corpus
extension are documented in
`docs/OOT3D_NRI_PICA_AOT_RENDERER_PARITY.md`.

## Build model

1. `Build-Oot3dWholeAotProduct.ps1` verifies immutable game inputs, generates
   256 C++ shards, and builds one content-addressed static archive with pinned
   LLVM 22.1.6.
2. The cache key includes the code image, manifests, generator, architectural
   state ABI, runtime headers, VFP helpers, and product CMake project.
3. `Configure-Oot3dWholeAotProduct.ps1` links the archive into a strict
   NRI/Vulkan consumer. Daily runtime edits do not rebuild the 12,419-function
   archive unless one of its real inputs changes.

```powershell
.\scripts\oot3d\Build-Oot3dWholeAotProduct.ps1 -Parallel 4
.\scripts\oot3d\Configure-Oot3dWholeAotProduct.ps1
.\scripts\oot3d\Build-Oot3dLlvm.ps1 `
  -BuildDirectory I:\oot3dre_work\whole-aot-product-consumer `
  -Target oot3d_native_game -Parallel 2
```

## State handoff correctness

Generated functions keep architectural state in native locals. A nested call
that yields to an observable host boundary must preserve the deepest
materialized guest state; an outer scope must not overwrite it while unwinding.
`Oot3dAotScopeExit::Dismiss()` and the state-bearing observable-exit snapshot
implement this rule for direct, indirect, and computed calls. Unit tests cover
both a dismissed stale commit and snapshot restoration.

## Repeatable equivalence

`Test-Oot3dWholeAotProduct.ps1` creates isolated graphics and savedata sandboxes
for the historical working oracle and the strict product. It disables VSync,
fixes host geometry and time, compares framebuffer checkpoints byte for byte,
optionally compares mixed PCM WAV output, and rejects every fallback/fault
counter. Synchronous framebuffer readback perturbs timing, so its FPS value is
diagnostic only and is not a throughput benchmark.

`Test-Oot3dWholeAotProductSuite.ps1` runs three surfaces:

1. boot and animated open-title through frame 900;
2. Kokiri cutscene/rendering for 481 presentation frames;
3. free Kokiri gameplay with deterministic movement input and PCM output.

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotProductSuite.ps1
```

Validated on 2026-08-23 before suite aggregation was added:

| Surface | Checkpoints | Result |
| --- | ---: | --- |
| Boot/open-title, frames 0..900 | 10 | all framebuffer hashes identical |
| Kokiri cutscene, 481 frames | 9 | all framebuffer hashes identical |
| Kokiri gameplay input, 61 frames | 3 | framebuffer and PCM identical |

Across these runs the strict product reported zero retained fallback, memory
fault, unsupported exit, and block-limit exits.

### Product-only golden release gate

The historical oracle is retained as provenance, not launched during routine
release validation. Its accepted outputs are pinned in
`tools/oot3d/native_a32_runtime/whole_aot_product_golden.json`, including the
source-suite SHA-256, exact framebuffer checkpoints, PCM hashes, execution
counts, and identities of every external input artifact.

`Test-Oot3dWholeAotGolden.ps1` verifies those artifact identities, creates a
fresh graphics and savedata sandbox for every run, launches only the strict
product, and rejects any mismatch in output, audio, execution counts, strict
counters, process exit, or crash dumps. The launcher runs in an isolated
PowerShell process so its exit status cannot terminate the aggregate gate.

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotGolden.ps1 -Repetitions 2
```

The 2026-08-23 release gate completed six isolated runs: two repetitions of
each of the three scenarios, 44 exact framebuffer comparisons, four exact PCM
comparisons, and 3,904,236 whole-AOT entries. Every comparison passed and all
strict counters remained zero.

### Automated release gate

`Test-Oot3dWholeAotRelease.ps1` composes the product checks into one command:

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotRelease.ps1 -RequireCleanWorktree
```

The gate validates the existing CMake cache and content-addressed archive,
links only stale consumer targets, runs the A32 ABI, savestate, PICA replay,
frontend, TopScreen, gameplay-runtime, and crash-diagnostic tests, executes the
product-only golden matrix and mandatory cross-process resume matrix, enforces
the NRI/Vulkan throughput floor, builds the relocatable diagnostic package,
and boots that package in isolation. Every tool that imports the Visual Studio
environment runs in a separate PowerShell process, preventing cumulative
`PATH` growth.

Normal release validation uses
`Configure-Oot3dWholeAotProduct.ps1 -ValidateOnly`, which checks compiler,
archive, generated program, product
mode, backend, overlay, and fallback settings without regenerating CMake. Use
`-ReconfigureConsumer` only when configuration inputs changed and
`-RebuildArchive` only when the content-addressed whole-AOT inputs changed.

The clean release on commit `fb6fe6d6d` completed in 554.3 seconds, including
the mandatory eight-scenario cross-process resume matrix. The content-addressed
639.62 MB archive was not rebuilt. The three throughput runs measured 99.92
median FPS and 99.73 minimum FPS at 1280x720 with VSync and pacing off. All
host tests, six golden runs, resume checks, package creation, and isolated
package boot passed.

### Cross-process resume equivalence

Portable savestates now include the temporal state that affects future output,
not only guest RAM and CPU registers. This covers the gameplay and presentation
clocks, TopScreen transition latches, pending PICA work, visual-frame history,
continuity tracking, display transfers, presentation images, color/depth and
auxiliary target attachments, Shadow2D data, decoded texture cache entries, and
the deterministic geometry/texture snapshot-cache identities. Host GPU handles
remain deliberately absent; Vulkan resources are recreated from portable CPU
snapshots and guest memory.

`Test-Oot3dWholeAotResumeMatrix.ps1` compares one uninterrupted run against a
run split across two independent processes. It requires exact equality of the
terminal framebuffer, concatenated PCM, savedata, memory content, guest frame,
whole-AOT execution counts, and normalized semantic savestate fingerprint.
The resume and soak harnesses derive isolated `1280x720`, 60 Hz configurations
with VSync disabled instead of allowing the user's display settings to
override their command-line test contract.
Successful multi-hundred-megabyte intermediate states are deleted only after
their identities have been recorded; failing scenarios retain full evidence.

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotResumeMatrix.ps1
```

The 2026-08-24 matrix passed all eight catalog scenarios across 24 isolated
processes and 3,960 product frames. It covers file select, Link's dream, Navi's
Kokiri flight and scene load, the Kokiri preload transition, Link's house,
outdoor gameplay with HUD, the ladder interaction, and the TopScreen items
page. Every strict runtime counter remained zero. The final defect exposed by
the gate was process-local PICA geometry-cache versioning: cache metadata is
now serialized deterministically while its byte payload is reconstructed from
the already-saved guest memory.

The same gate also exposed a validation defect in the generated controller
timelines. Savestates preserve their absolute guest frame, while the soak and
resume harnesses had emitted segments starting at zero. Their scripted input
was therefore enabled but inactive. Timeline schema v1 now has an explicit
`frame_origin`: existing authored traces default to `guest`, while generated
test traces use `run`. The runtime reports that origin and the release gate
requires every generated segment to be sampled. Non-idle continuous scenarios
must also consume an actual button, circle-pad, or touch action. Resume
equivalence now compares those input counters as well as game output. The
corrected matrix again passed 8/8, including 178 active circle-pad frames in
the Kokiri gameplay scenario and the forward ladder interaction.

### Deterministic soak campaigns

`Test-Oot3dWholeAotSoak.ps1` extends validation beyond fixed goldens. It splits
the run into bounded chunks and executes every chunk twice from the same
savestate with the same seeded input. It requires exact equality of the ending
savestate, framebuffer, PCM, savedata, runtime fingerprints, and whole-AOT call
counts before using that state as the next chunk's input.

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotSoak.ps1 `
  -Chunks 10 -FramesPerChunk 600
```

At the first process failure or deterministic mismatch, the harness stops and
retains the chunk's starting savestate, complete input prefix, both lane
artifacts, logs, runtime reports, and any crash dump. Successful duplicate
media and intermediate states are deleted after hashing, keeping long runs
bounded in disk usage. Pass `-KeepSuccessfulMedia` only for visual audits.

The existing 1:64 block profiler is summarized against the packaged guest map.
This produces a conservative observed-owner coverage measure without adding
counters to generated functions or invalidating the static archive. The
2026-08-23 five-chunk campaign executed 3,000 product frames, observed
278,206,138 block entries and 1,037 of 12,419 owners (8.35%), attributed every
reported sample, and found no lane mismatch. Its retained artifacts occupied
78.34 MB. Add `-SoakChunks <count>` to the release gate to run this optional
long-duration phase after package validation.

After portable visual replay was completed, a three-chunk campaign was rerun
on 2026-08-24 with run-relative exploration input. Six independent processes
executed 1,080 product frames through two consecutive state handoffs, sampled
all 180 input frames in every chunk, observed 108,059,060 block entries and
1,042 of 12,419 owners (8.39%), and produced no lane mismatch. The previous
nominal exploration run, which was actually neutral because of the frame-origin
defect, had observed 1,024 owners. The final state was discarded after
validation.

### Scenario coverage matrix

`whole_aot_product_scenarios.json` catalogs external checkpoints by stable
name, category, byte count, SHA-256, input profile, duration, and seed. These
states extend deterministic execution coverage; they are deliberately not
visual goldens unless a separate controlled oracle is recorded. The release
gate always validates the catalog and every state identity.

`Test-Oot3dWholeAotScenarioMatrix.ps1` runs each selected scenario through the
same two-lane comparison used by the soak harness, continues after an isolated
scenario failure, and merges every runtime profile into one owner-coverage
report. Input generation is explicit: `idle`, `forward`, or deterministic
`exploration`.

```powershell
.\scripts\oot3d\Test-Oot3dWholeAotScenarioMatrix.ps1
.\scripts\oot3d\Test-Oot3dWholeAotRelease.ps1 `
  -RequireCleanWorktree -RunScenarioMatrix
```

The 2026-08-23 matrix covered file select, Link's dream, Navi's Kokiri flight
and scene load, the Kokiri preload transition, Link's house, outdoor gameplay
with HUD, the ladder interaction, and the TopScreen items page. All eight
scenarios passed across 18 isolated process runs and 3,960 product frames. The
aggregate profile observed 295,242,708 block entries, 8,643 guest PCs, and
1,388 of 12,419 owners (11.18%), with no unmatched reported sample.

This matrix exposed a portable-savestate defect: the serialized PICA
submission state retained addresses of GPU-backed render targets even though
the Vulkan images themselves are host-only and cannot survive process reload.
Restoration now validates the legacy inventory without treating it as live;
display transfers remain on the CTR memory path until a new draw explicitly
re-establishes GPU ownership. The submission test proves both halves of this
transition, and the previously failing TopScreen state now completes
deterministically.

For scene and renderer expansion without one manually captured state per
location, use the native entrance matrix documented in
`OOT3D_STRUCTURAL_SCENARIO_COVERAGE.md`. It derives 82 recipes across 27 scenes
from copied decomp evidence, delegates construction to the original loader,
and closes each discovered PICA surface with a second strict AOT-shader run.
The current representative matrix passes 27/27 scenes and 470,104 strict
native draws with zero shader misses, including the native PICA ProcTex path.

## Throughput baseline

Use `Measure-Oot3dRuntimePerformance.ps1`, not the framebuffer equivalence
harness. On the Kokiri gameplay checkpoint at 1280x720, with deterministic
movement, VSync disabled, 60 warmup frames excluded, and 300 measured frames:

| Runtime | Median FPS | Minimum run FPS | Draws per run |
| --- | ---: | ---: | ---: |
| Strict full whole-AOT product | 99.92 | 99.73 | 16,277 |
| Historical working oracle | 87.48 | 86.07 | 16,277 |

The product therefore has no measured whole-AOT throughput regression on this
surface and remains above the 60 FPS target.

## Local relocatable bundle

The package script copies the strict executable, its one non-system DLL,
controller database, runtime resources, UI/control configuration, product
metadata, and the user's extracted game blobs. It rewrites the process manifest
to relative paths and marks the resulting package as non-redistributable.

```powershell
.\scripts\oot3d\Package-Oot3dWholeAotProduct.ps1
.\scripts\oot3d\Test-Oot3dWholeAotPackage.ps1 `
  -PackageDirectory I:\oot3dre_work\whole-aot-product-packages\<package>
```

The package test verifies every inventoried SHA-256, rejects absolute game-data
paths, boots through frame 300 from inside the package, checks for a nonempty
framebuffer, and enforces the strict runtime counters.

The release bundle from commit `fb6fe6d6d` contains 171 inventoried files
(757,001,048 bytes), including the product golden contract and input-timeline
test closure. Its validated local path is
`I:\oot3dre_work\whole-aot-product-packages\oot3d-whole-aot-release-4d27d0453f42`.
Its isolated frame-300 SHA-256 was
`c188d6ea67d4f31547d623df018061e9ef03a9f66d29246eb7a882fd296ad6b3`,
identical to the controlled oracle checkpoint.

## Native diagnostics

Product-mode Windows links emit a full PDB and linker MAP without adding debug
code to the Release executable. `generate_whole_aot_guest_map.py` joins the
structural program, generated shard manifest, and native linker MAP into one
deterministic address map containing:

- all 12,419 guest functions and their native x64 symbols/RVAs;
- all 161,332 effective dispatcher PCs and their deterministic owners;
- 14,551 exact guest code ranges, including discontiguous recovered owners;
- shard ownership, resume entries, block counts, and instruction counts.

The local package stores the 395 MB PDB and 95.8 MB linker MAP as 90.3 MB and
5.7 MB ZIP archives. `symbols/symbols.json` binds the executable, uncompressed
PDB, linker MAP, and guest map with byte counts and SHA-256 digests. Package
validation hashes the contents inside both archives and verifies that the
process entry resolves from guest PC `0x00100000` to a native x64 RVA.

Product mode also installs an isolated Windows exception filter after the
three_ds_recomp_runtime crash handler. Unhandled exceptions write timestamped minidumps to
`savedata/crashes` with thread, module, handle, data-segment, and indirectly
referenced memory state, then chain to the existing text report. A dedicated
test validates both direct dump generation and a real unhandled exception in a
bounded child process.

The first captured product dump exposed a renderer startup race: the Azahar
texture worker was launched from a member initializer before the queues and
stop flags it reads had been constructed. Worker startup now occurs in the
constructor body. The formerly failing empty-savedata run completed 61 frames
with 74,926 whole-AOT calls and zero strict failures; the packaged 301-frame
checkpoint remained byte-identical.

`Resolve-Crash.ps1` parses Windows minidumps using only Python's standard
library. It verifies the dumped module timestamp and image size before using
symbols, resolves host crashes to the nearest linker-MAP symbol, and resolves a
whole-AOT instruction pointer only when it lies inside the exact PE unwind
extent of a generated function. All 12,419 generated functions have exact PE
extents in the current product. A dump from the pre-fix executable was
correctly rejected against post-fix symbols instead of being mis-symbolized.

## Remaining maturity work

- Grow the checkpoint suite across scene transitions, menus, save/load, combat,
  bosses, items, and ending flow.
- Run deterministic campaigns from broader route checkpoints and seeds to
  increase observed owner and gameplay-system coverage.
- Keep renderer, audio, UI, and host-service work outside generated AOT shards
  so those subsystems retain fast incremental builds.
