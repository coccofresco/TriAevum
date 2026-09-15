# Structural stuttering resolution

Date: 2026-09-15. Baseline: `a9d9441`, branch `port/linux-nri`.
Status: current optimization phase closed by user decision on 2026-09-15.
The measured result is sufficient for this phase, not proof of zero stuttering
or full-plan completion. Remaining work is deferred to future optimization;
the closure note at the end supersedes earlier immediate next-step instructions.

This is the forward plan. Historical evidence remains in
[the native shader analysis](TRIAEVUM_OOT3D_UBERSHADER_ANALYSIS.md).
The [NRI architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md) remains
authoritative. Do not restart the migration or replace the renderer wholesale.

## 1. Outcome and boundaries

Render native OOT3D and its supported extensions without generating/compiling
shader source during gameplay, and remove expensive driver compilation from
interactive draw submission. Achieve this through maintained programs, native
state as data, reusable device pipeline parts and explicit preparation/lifetime.

Three different problems must remain distinguishable:

| Work | Desired location | Completion evidence |
| --- | --- | --- |
| Shader source to portable SPIR-V | Developer build | Zero runtime source compiler/cache resolver requests |
| SPIR-V and immutable state to device programs | Device/profile preparation, then resource preparation | Measured preparation, part reuse, no unexpected expensive compilation during draws |
| Resource uploads, submission, GPU execution, gameplay | Runtime | Separately attributed frame-time tails, not called shader stutter |

SPIR-V does not remove the driver's need to produce GPU machine code. A shader
pack with zero misses also does not prove zero pipeline stalls. Conversely,
maintaining live device objects is not a dependency on a harvested shader cache.
Driver compilation at explicit preparation is acceptable; silently moving the
same blocking work earlier in the same interactive frame is not a solution.

Constraints:

- No scene/asset exceptions, skipped draws, approximate substitute materials,
  stale-frame presentation, altered gameplay speed or concealed missing shaders.
- Native depth, stencil, blend, ordering, lighting and composition remain exact.
  World effects cannot consume or modify HUD/menu layers.
- Optional effects are inert when disabled. Numerical controls become data where
  semantically possible; they must not manufacture new programs on every change.
- Keep specialized rendering as a diagnostic reference, not an unnoticed escape
  from the new path. An unsupported case is reported, not counted as completed.
- Reuse current evidence; external decompilation repositories stay read-only.
  Request a specific missing contract only when implementation needs it.
- Collected caches, TAS replay and more manual gameplay are not prerequisites
  for this solution. Existing caches may accelerate legacy execution, but the
  new path must pass with them absent.

## 2. Established baseline, not promises

| Change | Commit | What is established |
| --- | --- | --- |
| Independent NRI/Vulkan pipeline ownership | `fd9195a` | No mandatory second Vulkan pipeline for NRI draws |
| Exact normalized device identity | `aaa3530` | Equal programs/layout/state share an object; active state remains in the key |
| Toon values moved to uniforms | `8735f46` | Appearance changes no longer change the base toon program |
| Toon plus temporal program family | `4d0e39b` | 116 built-in fragment artifacts in total; tested combination needs no collected pack |
| Lazy fallback shader modules | `e6490d7` | NRI-only draws create no Vulkan fallback shader pairs |
| Bounded live device object retention | `a9d9441` | Active aliases pin objects; up to 128 idle objects survive logical retirement |

Windows boot/Off and Field toon+TAA comparisons pass on the documented fixtures.
This is not full-game or all-effects coverage. The retention change has unit
lifetime tests and steady-configuration game comparisons, but no timed repeated
MSAA transition test yet. Older isolated TAA-only/baked-toon pixel differences
remain recorded in the analysis; do not silently count them as fixed.

Relevant measurements:

- A new toon/temporal program run spent 3.153 seconds cumulatively creating NRI
  pipelines. Later runs took roughly 0.06 seconds, with internal driver caches
  not cleared: that difference is not a demonstrated optimization.
- Eliminating fallback shader duplication reduced specialized compilation from
  50 programs to 25. The tested parametric path already compiled zero programs.
- The latest short parametric runs measured about 99-101 native frames/s without
  interpolation, yet an application-empty run still had a roughly 236 ms maximum
  frame. Average FPS alone is therefore an inadequate success criterion.
- In a 300-frame compiler-free reference, guest execution took 1.645 seconds and
  backend replay 0.593 seconds. These are not shader compiler costs.

## 3. Order of implementation

| Priority | Work | Required deliverable |
| --- | --- | --- |
| P0 | Expensive driver pipeline creation | Real NRI draws using reusable pipeline parts, with measured link/creation time |
| P1 | Missing native/effect program coverage | Supported profiles execute without source generation/compilation or collected packs |
| P2 | Repeated invalidations and lifetime | Repeated state/profile transitions reuse compatible objects without leaks or stalls |
| P3 | Remaining frame-time tails | Fix the largest independently measured upload/submission/GPU bottleneck |

P0 and the P1 default-profile audit may advance together. Do not defer P0 for
another round of tiny key deduplications or artifact-count increases. Reorder
only when matched measurements show a larger avoidable cost, and record why.

### P0: one real pipeline-library vertical slice, then generalize

**First deliverable:** one existing canonical NRI draw family uses independently
owned pipeline parts, links, draws and matches its monolithic reference. It must
exercise actual driver calls, not only key generation or a mock backend.

1. Query supported extensions/features/properties on the selected device. Record
   support, enablement and execution separately. Query
   `VK_EXT_graphics_pipeline_library`, its dependencies, fast-linking support and
   the dynamic states actually available. Pass the enabled capabilities honestly
   to the wrapped NRI device; never infer support from vendor or OS names.
2. Extend the pinned NRI Vulkan factory at its owning boundary. Prefer a narrow,
   versioned project patch/extension with an explicit interface. Carry it through
   CMake and the source package; do not edit an external checkout as the only
   implementation. Preserve NRI descriptor layout and command binding ownership.
3. Build typed identities for the four library subsets and final executable.
   Include all state consumed by each subset and every relevant interface/layout
   version. Do not hash structure padding, pointers or raw `pNext` addresses.
4. Initially keep the existing full descriptor layout. Avoid simultaneous layout
   redesign, new shader math, dynamic-state expansion and library integration.
5. Prepare the expensive shader-bearing parts before their first interactive
   draw. Link already prepared parts without link-time optimization in the
   latency-sensitive path, subject to measured device behavior.
6. Keep the same logical pipeline identity for drawing. Device objects have
   explicit owners; replacements/retirement respect in-flight GPU work. Connect
   the existing exact identity and bounded retention instead of adding competing
   global registries.

Vulkan separates vertex input, pre-rasterization, fragment shader and fragment
output state. Shared state must agree across parts. Fragment shader state includes
depth/stencil requirements; splitting does not make every fixed-function value
dynamic. Follow the actual specification rather than assuming one library per
SPIR-V binary. [Khronos design and state partition](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_graphics_pipeline_library.html).

`graphicsPipelineLibraryFastLinking` describes the advertised non-LTO link cost;
it must be queried, not assumed. The supported path still needs measured CPU
link and GPU execution times. [Khronos property contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT.html).

After the vertical slice passes, cover every immutable state accepted by the
current PICA factory and the fragment output contracts used by extensions. Add
dynamic states only as a separate measured change, with matching bind commands
and feature enablement. Never remove depth/blend/cull fields merely to reduce
the pipeline count.

**GPU throughput safeguard:** fast-linked pipelines may execute more slowly.
If needed, build optimized equivalents on a bounded worker queue and promote
them at a safe frame boundary. Use the same programs and state; do not reroute
draw order. Expensive optimization must not block the rendering thread.
[Khronos sample and optimization tradeoff](https://docs.vulkan.org/samples/latest/samples/extensions/graphics_pipeline_library/README.html).

### P0 portability and preparation policy

| Device capability | Policy |
| --- | --- |
| Libraries plus fast linking | Prepare costly parts; measure on-demand final links; retain reusable executables |
| Libraries without fast linking | Prepare/link complete needed executables before interactive use |
| No library support | Preserve the exact monolithic factory; prepare needed pipelines outside draw submission |

Use the same typed preparation requests for all paths. Requests come from the
maintained program family, decoded material/draw state and the compiled effect
graph, not scene names or a captured shader inventory. Include non-CMB draws,
native runtime state changes and extension interfaces; CMB materials alone are
not a complete pipeline recipe source.

Coalesce identical pending requests. Give work owned input data and cancellation
by device generation, not references into transient draw packets. Start with a
small bounded queue; obey driver/cache synchronization requirements and prevent
background compilation from starving gameplay. Use real loading/preparation
boundaries rather than manufacturing periodic blocking pauses.

Do not prebuild the Cartesian product of every register value. Reduce numerical
values to uniforms; prepare only structurally reachable contracts. A genuinely
unknown required pipeline must preserve correctness and report its synchronous
cost if unavoidable. It is then an unresolved preparation miss, never a hidden
success. Zero stalls on all unsupported hardware is not an honest unconditional
promise. Persistent caches can remain optional accelerators, never correctness
or acceptance dependencies.

### P1: close programs by semantic family, not captured examples

Start with the actual shipped/default effect combination, then its reachable
controls and Off transitions. The earlier base-toon benchmark excludes outline
and is not a substitute for testing the default configuration.

1. Classify remaining source/compiler requests by owning feature and interface:
   native vertex programs, fragment behavior, compatibility passes, outline/
   fog/normal/domain guides, AO, reflections and directional shadows.
2. For each family, separate data from structure. Reuse canonical PICA equations
   and typed hooks. Uniformize continuous values before generating artifacts.
3. Compile the small structural program family in the developer build. Compose
   only reachable output/interface combinations from the effect graph. Track
   artifact size, build time and startup preparation cost; 116 is the present
   count, not a target or permission for unlimited permutations.
4. Validate Off invariance, lighting/LUT/TEV semantics, reactive coverage,
   transparent ordering and UI exclusion. Programs for extra outputs must not
   silently force attachments or effects onto canonical draws.
5. Audit `CmbVShader.shbin`, `profile.shbin` and already-known native consumers
   against the existing offline vertex family. Use pinned evidence from the
   analysis; do not re-run decompilation or gameplay surveys without a specific
   missing semantic case.

Strict validation must fail on an unexpected runtime source compiler request.
During migration, any legacy fallback remains explicit in diagnostics and is
excluded from claims that the new path is complete. Remove the compiler from
the new path's required runtime dependencies only after the contract is closed.

### P2: verify lifetime, not just retained-object counts

Exercise `1x -> 4x MSAA -> 1x`, AA/effect Off/On, internal-resolution changes,
window resize/fullscreen, scene transitions and device recreation. Unchanged
program/layout/state must reuse an object while retained; genuinely changed
contracts must remain distinct. Test enough distinct inactive objects to exceed
the retention limit. Never evict in-flight work or keep references to a dead
layout/device. Include framebuffer checks before and after returning to a state.

Do not add more retention policy until these tests establish whether the existing
128-idle-object limit removes repeated driver work and what memory it costs.

### P3: attribute the remaining tails

Once source/driver costs are separated, inspect CPU submission/planning, redundant
copies/uploads, allocation, resource retirement, synchronization and GPU passes.
Use CPU timings and asynchronous GPU timestamps; no forced GPU wait per draw
for profiling. Rank by accumulated cost and worst-frame contribution.

Fix the largest avoidable cause at its owning module. Do not conflate grass/AA
GPU cost, asset loading, guest execution or frame pacing with shader stuttering.
No new gameplay/AOT optimization campaign unless the measurements justify it.

## 4. Ownership and entry points

Paths below are relative to the repository root; names marked proposed do not
claim an existing implementation.

| Boundary | Current code / intended responsibility |
| --- | --- |
| Device and NRI integration | `runtime/three_ds_recomp/src/fast/backends/gfx_vulkan.cpp`; `src/fast/oot3d/nri_interop_context.cpp` under the same runtime; negotiate device features and connect a reusable factory |
| NRI native factory | `runtime/three_ds_recomp/src/fast/renderer3ds/nri_pica_pipeline_bridge.cpp`; keep descriptors, binding and ownership coherent |
| Shared identities/lifetime | `runtime/three_ds_recomp/include/fast/renderer3ds/{pica_pipeline_identity,nri_pica_pipeline_identity,pica_device_pipeline_pool}.h` |
| Shared contract | `runtime/three_ds_recomp/include/fast/renderer3ds/pica_nri_shader_contract.h`; proposed pipeline-part/preparation types belong beside this layer, with no OOT3D addresses |
| NRI donor changes | `runtime/three_ds_recomp/cmake/patches/` and `CMakeLists.txt`; explicit pinned patch, reproducible on every platform, notices preserved |
| Native lowering | `tools/oot3d/native_pica_frontend/`; use existing SHBIN/material/register evidence |
| Extension composition | `runtime/three_ds_recomp/src/fast/oot3d/pica_shader_{pipeline_cache,instrumentation}.cpp` and feature-owned modules; no string-patched scheduling |
| Developer artifacts | `tools/renderer/tev_program/build_{vertex,fragment,pass}_artifacts.cpp` |
| Verification | `tools/renderer/tev_program/{run_native_comparison,benchmark_native_paths}.py`, GPU tests and identity/lifetime tests |

Generic Vulkan/NRI pipeline mechanics must not acquire title-specific behavior
because the current integration entry point happens to live under `oot3d`.
Avoid an unrelated directory migration during the critical implementation.

## 5. Measurement and acceptance

Use the existing private invocation fixture; copy config/save data per run.
Baseline and candidate use the same content, state, resolution, effect settings,
driver and device. Record executable/commit and program-family identities.

Run three distinct windows, never average them together:

1. Explicit startup/resource preparation: elapsed time, jobs, bytes and device
   compilation/link counts. Report separately, rather than hiding startup cost.
2. First rendered use: zero benchmark warmup, application caches absent, including
   the first native frame and transitions. State whether driver caches are warm,
   cold or unknown; no destructive global driver-cache clearing is required.
3. Steady state: fixed warmup, repeated runs with rotated baseline/candidate order.

For timing: native30 fixed delta, no interpolation, VSync, pacing, limiter,
screenshots or synchronous per-draw diagnostics; neutral input. Verify that
native updates and presented frames agree. Report p50/p95/p99/max, counts beyond
16.67/33.33 ms and separately source compile, part compile, link and preparation
wait times. Repeat at least three times when claiming a speedup. A warm driver
run is not proof of a cold-start fix.

For correctness: separate deterministic framebuffer runs, comparison to the
specialized path and selected existing emulator evidence. Cover boot/title,
Kokiri, wide Field, a transparent-heavy case, and HUD/menu composition. Use the
existing fixtures; add a new scenario only to exercise a missing contract.

Acceptance requirements:

- Zero runtime source compiler/cache resolver requests for the declared new-path
  native and enabled-effect surface, with collected packs absent.
- Exact-key part preparation is coalesced and not repeated merely because final
  blend/output combinations change. Count each legal part identity, not merely
  unique SPIR-V binaries.
- No unexpected expensive driver creation in the declared prepared interactive
  workload. Report missing contracts and worst link times explicitly.
- Provisional fast-link budget on the reference device: p99 <= 1 ms, maximum
  <= 4 ms of render-thread driver work after preparation. These are acceptance
  targets to measure, not guarantees for every GPU or claims already achieved.
- No material steady-state regression: investigate a repeatable >5% increase in
  GPU/frame time before enabling the library path by default. Do not exchange
  fewer hitches for permanently slow uber-shader execution.
- No new framebuffer/order/UI regression; preserve and identify existing known
  discrepancies instead of silently widening tolerances.
- Verify Windows and Linux GPU execution, then Android capability/fallback
  behavior. Shared code is not a substitute for driver testing. Without a Steam
  Deck, report platform conformity checks, not tested Deck performance.

The new path becomes default only for the verified surface/capability policy.
Do not remove the diagnostic monolithic/specialized comparison before that.

## 6. Execution discipline and replanning

Deliver vertical milestones, with code, real draws and evidence in each commit:

| Milestone | Exit criterion |
| --- | --- |
| M1 | Capability negotiation and one real NRI library-backed family draw correctly; creation/link timings recorded |
| M2 | General native pipeline states and preparation/lifetime use that factory; portability fallback remains correct |
| M3 | Shipped default effects and their supported combinations need no runtime source compilation |
| M4 | State-transition tests, measured tail reduction and Windows/Linux/Android qualification complete |

After each bounded experiment, decide explicitly: retain, revise, or discard.
Do not spend consecutive tranches on counters, tiny alias reductions or isolated
tests without advancing the live consumer. A mock library or increased artifact
count cannot satisfy M1/M2.

If GPL is unsupported or loses too much GPU throughput, switch to the shared
explicit-preparation monolithic path for that capability set. If reachable states
cannot be prepared in time, identify the missing load/command boundary before
adding workers or a bigger cache. Consider shader objects only after a small
measured prototype shows a benefit worth a second device-command strategy.

Use bounded incremental runtime builds, initially two compiler jobs on this
machine; no title whole-AOT regeneration for renderer edits. Keep generated shader
tables in owning translation units, not widely included backend headers. Update
the build lane when new source files are added; do not accidentally test an old
executable. Run focused tests first and broader GPU comparisons at each integrated
milestone, not after every helper edit. No mandatory long gameplay automation.

Release changes must respect the [precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md).
Forge must not acquire a compiler or regenerate title code. Maintained renderer
artifacts and audited donor patches are developer/package responsibilities;
device-specific preparation stays local. The existing legacy bundled corpus can
remain for compatibility during migration, without becoming a requirement of the
new path. Do not publish private ROMs, extracted assets, saves, captures or driver
caches with test evidence.

## M1 implementation and measured limits (2026-09-15)

M1 now has a real Vulkan/NRI consumer, not just a proposed key or mock library.
The reproducible donor patch is
`runtime/three_ds_recomp/cmake/patches/nri_graphics_pipeline_libraries.cmake`;
its layout-owned implementation is
`nri_pipeline_libraries/GraphicsPipelineLibrariesVK.h` beside that patch.
The CMake source-checkout and FetchContent paths both apply it. ABI version 1
is required by the shared renderer bridge. No title code was regenerated.

The opt-in `TRIAEVUM_NRI_PIPELINE_LIBRARIES=1` negotiates both Vulkan library
extensions and the feature on the actual device, including the NRI wrapper.
It preserves shader bytes, descriptors, draw commands and native fixed state.
Four exact-state subsets are retained independently, bounded to 128 entries
per subset per layout, and destroyed before their layout/device. Executable
pipelines retain the existing NRI identity and lifetime ownership. Unknown
extension/state contracts explicitly fall back and increment a rejection count.
This first implementation accepts the existing vertex+fragment dynamic-rendering
contract, not arbitrary mesh/tessellation/specialization/pNext contracts.

Windows RTX 3060 validation, without a collected shader pack:

| Case | Executables linked | Parts created (input/pre-raster/fragment/output) | Maximum link |
| --- | ---: | --- | ---: |
| Field canonical | 24 | 8 / 3 / 4 / 6 | 0.163 ms |
| Field toon + TAA, outline off | 27 | 8 / 5 / 7 / 8 | 0.161 ms |
| Boot, effects off | 25 | 10 / 2 / 4 / 7 | 0.197 ms |

All three cases produced three pixel-identical framebuffer comparisons against the
specialized path, zero runtime source compilations and zero library rejections.
Canonical GPL also matched the same executable's monolithic parametric output
exactly at all three frames. Vulkan and NRI validation reported zero errors.
Validation warnings are not claimed absent; these remain recorded in the private
renderer reports. The focused mock test verifies subset reuse, equal shader
bytes at different addresses, depth/output separation, unknown-state rejection
and bounded retirement. It is supplementary to the actual draw tests.
The complete local TEV/lighting/procedural-texture/artifact/identity suite passed
13/13 tests. Linux and Android GPU qualification have not been run for GPL.

Important: fragment-part creation still ran synchronously and cost 327.5 ms
cumulatively for canonical and 1083.2 ms for toon+TAA on those correctness runs.
Those runs include validation and are not throughput benchmarks. Fast final
links do NOT mean expensive work has left the interactive path.

Three rotating-order, 300-native-frame runs including first use measured median
101.33 FPS monolithic versus 95.91 FPS GPL (about 5.3% lower throughput).
VSync, interpolation, pacing, limiter and captures were disabled; both arms
used the same parametric programs with zero source compiler requests. Driver
caches were not cleared. This is NOT a speedup or a cold-driver claim.
Keep GPL opt-in until the throughput safeguard and preparation boundary are
resolved. Do not make it the default merely because final links are cheap.

The separate steady-state experiment (three runs of 900 frames, first 180
excluded) measured medians 114.73 FPS monolithic and 116.67 FPS GPL, with GPL
runs ranging 112.57-118.01 FPS. This does not establish a material steady-state
regression; nor does the small median gain establish a general speedup. GPL
p99 median was 11.625 ms, maximum 17.078 ms, with zero frames over 33.33 ms
across 2160 measured frames. The priority remains first-use preparation, not
an assumed permanent shader-execution penalty. Evidence is in
`%TEMP%/TriAevum-gpl-steady-20260915`.

Reproduction tools:

- `run_native_comparison.py --pipeline-libraries --validation --no-shader-pack
  --require-no-runtime-compilation --require-single-pipeline-owner`, with the
  existing `--invocation` and a new `--output`; add `--toon --taa` as needed.
- `benchmark_native_paths.py --compare-pipeline-libraries --frames 300
  --warmup 0 --repeats 3`, with that invocation and a separate output. This
  compares the same parametric programs, not specialized versus parametric.
- Private evidence: `%TEMP%/TriAevum-gpl-{field,monolithic,toon,boot,timing}-20260915`.
  Do not package these directories or their game-derived inputs.

## M2 native startup preparation (2026-09-15)

The native 1x-MSAA family now has an explicit startup consumer. This is partial
M2 completion, not default-effect or full-platform completion.

1. The opt-in GPL capability policy additionally requires and enables
   `VK_EXT_extended_dynamic_state` and native fragment storage writes. The NRI
   wrapper receives the actual enabled extension list.
2. `DynamicPipelineStateVK.h`, carried by the reproducible NRI patch, preserves
   each executable's original culling, front-face, depth and stencil values.
   Every bind reapplies them through Vulkan commands. Only declared dynamic
   fields leave library identities; sample count/output interfaces remain part
   of their contracts. Declaration order is normalized, not pointer-hashed.
3. The shared `PicaProgramPreparationBackend` interface receives a title-supplied
   vertex artifact family. The native window calls it immediately after host
   initialization, BEFORE the gameplay loop and first frame timing. This is
   synchronous, bounded startup preparation; there are no borrowed queued draw
   packets, synthetic gameplay frames or asynchronous lifetime assumptions.
4. Six maintained SHBIN-derived vertex programs and eight native fragment
   programs are prepared through the normal NRI factory. Thirteen preparation
   pipelines cover their independent subsets, rather than 48 vertex/fragment
   pairs or a Cartesian product of material registers. The preparation-only
   input layout is never used for a draw. Temporary linked executables are
   destroyed; expensive library parts stay with the real descriptor-layout
   owner and are reused by native draws.
5. Preparing all eight fragments exposed a missing device feature:
   `fragmentStoresAndAtomics` had not been enabled for native storage-image
   writes. The shared device profile now enables it when supported; unsupported
   devices do not enter this complete-family preparation policy. The original
   failing validation run is retained as evidence, not counted as a pass.

Windows RTX 3060 evidence, no collected pack:

- Field and boot: three exact framebuffer comparisons each, zero source
  compilation, zero validation errors, zero GPL fallback, and **zero new
  shader-bearing libraries after the startup preparation end marker**.
- Field also matches the prior monolithic parametric framebuffer exactly.
- Toon+TAA: three exact comparisons and zero validation errors/source compiler
  requests; additional effect libraries are still created after preparation.
  It is NOT counted as fully prepared. Outline/default-profile completeness
  remains an M3 item.
- Dynamic-state unit tests cover native bind values on both stencil faces,
  repeated binds, inert disabled behavior, dynamic identity normalization,
  unchanged MSAA separation and supported/unsupported fragment-write features.
- The full local renderer suite passed 13/13 tests after these changes. The
  pinned NRI patch was reapplied successfully to check idempotence.

Matched first-use experiment: three rotating-order runs of 300 native frames,
zero warmup, interpolation/VSync/pacing/capture disabled. Monolithic median
93.85 FPS, prepared GPL 97.63 FPS; maximum 202.21 versus 158.16 ms, but median
run p99 22.125 versus 31.0 ms. This mixed result is NOT a claim that all
stuttering is solved. Both arms used identical parametric programs. Driver
caches were not cleared. Preparation took 93.56 / 12.39 / 15.98 ms separately
from gameplay timing; maximum link across these runs was 0.049 ms. All three
GPL runs created zero shader-bearing parts after preparation. Remaining frame
tails cannot be attributed to those shader parts without contrary evidence.

Separate steady-state experiment (three 900-frame runs, 180 warmup): median
114.27 FPS monolithic versus 116.53 FPS prepared GPL. GPL range 109.24-117.34
FPS, maximum frame 19.13 ms, no frame above 33.33 ms across 2160 measured frames.
This does not establish a material steady-state loss; the small median difference
is not presented as a general speedup. Evidence:
`%TEMP%/TriAevum-gpl-prepared-steady-20260915`.

Reproduction: add `--require-prepared-native-programs` to the M1 canonical
comparison command. It fails on missing preparation or any later vertex/fragment
library creation. The benchmark's `--compare-pipeline-libraries` records startup
cost separately and asserts this invariant for its canonical arms.
Private evidence: `%TEMP%/TriAevum-gpl-prepared-{field-v2,boot,toon,timing}-20260915`.
The failed feature-negotiation run is `TriAevum-gpl-prepared-field-20260915`.

## Residual stall attribution and synchronization repair (2026-09-15)

Priority is now residual frame time, not more shader inventories. A fixed-storage
`SlowFrameSamples` collector retains the worst 16 measured frames with deltas of
the existing host/guest/replay timers. It performs no per-frame allocation or I/O.
The report explicitly identifies nested phases: they must NOT be added together.
An opt-in `TRIAEVUM_FRAME_START_TIMING=1` trace separates presentation settings,
present completion, transitions, frame fence, retirement, acquisition and finish.
Use that diagnostic only for attribution, not uninstrumented throughput claims.

The canonical Field fixture exposed two avoidable costs, both now corrected:

- The first swapchain used a default VSync value, then the first display-settings
  transaction recreated it even when the host window already matched. Init now
  uses the configured VSync value. The initial transaction adopts an existing
  healthy, matching window/swapchain; different settings, dirty surfaces and
  subsequent candidate/rollback transactions retain the normal apply path.
- Image ownership retained fence handles after their submission completed. When
  a frame slot reused the handle, acquiring an old image could wait on unrelated
  newer work. After the slot fence completes, its image references are retired
  BEFORE reuse. The slot wait, acquisition semaphore and waits for genuinely
  outstanding image submissions remain. No native draw or visibility is changed.

The fixes are in `GfxRenderingAPIVulkan::Init`, `ApplyPresentationSettings` and
`StartFrame`, not in an OOT3D scene adapter. Shared bounded attribution lives in
`runtime/three_ds_recomp/include/fast/renderer/slow_frame_samples.h`; the native
benchmark consumer writes `phase_timing.slow_frames` only for measured frames.

Windows RTX 3060 evidence (same Field fixture; 300 native updates, zero warmup,
no interpolation/VSync/pacing/limiter/captures; driver cache NOT cleared):

- Before, attribution run: first GPL frame 194.93 ms, renderer start 63.98 ms,
  replay 62.40 ms. Separate stage run: first-frame settings 67.37 ms; later
  finish phases included 21.24/23.28 ms in addition to frame-fence waits.
- After, three rotating-order repetitions: first GPL frames
  123.31 / 94.92 / 102.51 ms; renderer start 0.48 / 0.43 / 0.62 ms;
  replay 62.00 / 36.75 / 41.00 ms. Late finish phases in the traced slow frames
  are now sub-millisecond. Actual frame-slot GPU waits remain and are not hidden.
- Prepared GPL median 100.33 native frames/s (98.79-102.78), median-run p99
  24.125 ms, maximum 123.31 ms, four frames >33.33 ms out of 900. The earlier
  three-run preparation experiment had 97.63 FPS / p99 31.0 / max 158.16 ms.
  These historical comparisons are indicative, not an isolated speedup claim:
  run order and driver state can affect tails. The attributable removed work
  is the redundant display transaction and stale image-fence ownership.
- Canonical comparison: three exact framebuffer matches, including against the
  previous monolithic captures; zero validation errors, runtime shader compiler
  requests or shader-bearing library creation after preparation.
- Boot-from-start comparison also passes three exact captures with the same
  structural invariants. The local renderer suite passes 13/13 tests, including
  invalid samples, bounded eviction and stable ties for slow-frame attribution.
- Separate steady-state run with stage tracing OFF (three 900-frame runs,
  180 warmup): prepared GPL median 114.58 FPS (108.42-116.90), median-run p99
  12.0 ms, maximum 23.51 ms, zero >33.33 ms across 2160 samples. Monolithic
  median 114.43 FPS, p99 12.875 ms, maximum 18.10 ms, also zero >33.33 ms.
  This is comparable to the prior steady-state range, not a large mean-FPS gain.

Private reproduction evidence: `%TEMP%/TriAevum-tail-{attribution,stage,fixed,
correctness,boot,steady}-20260915`. Build remains the frozen `J:/TriAevum-verify-20260910/runtime`
lane with `ninja -f .tev-validation.ninja -j 2 TriAevum.exe`; no title-AOT rebuild.

This does NOT close stuttering. The next measured target is first replay
resource/setup work (37-62 ms here), then genuine GPU execution tails. Split
resource allocation, upload, draw preparation and execution before selecting a
fix; do not call these shader compilation without evidence. Preserve complete
native output and synchronization. Linux/Android qualification is still pending.

## Replay texture side work (2026-09-15)

The existing per-draw timers were sufficient to split the first replay. The
benchmark now accepts `--diagnostics`, retaining the renderer report and scheduler
timers without enabling Vulkan validation or framebuffer capture. Reports flag
these runs as instrumented; use the default mode for performance comparisons.

Canonical Field attribution before this change: texture phase 24.33 ms, geometry
and uniform upload phase 9.84 ms, pipeline phase 5.95 ms, total draw CPU work
47.94 ms on frame 1. This is NOT the full scheduler/replay time. Temporary tracing
showed the fixture restored decoded pixels: no live texture decoding occurred.
Across 80 image creations, allocation took 7.62 ms and GPU upload recording
1.57 ms. Optimizing the ETC decoder would not address this fixture's stall.
The temporary texture trace was removed after attribution.

`GrassTextureSourceCache::ObserveDecoded` was hashing all decoded RGBA pixels to
build aliases for older decoded-model consumers, even when rendering and effects
used encoded/native content identities only. That compatibility index is now
materialized per dimension pair on its first actual query. Native-key masks,
previews, average colors and the 4x4 color grid remain immediately available.
Source pixels and identifiers are unchanged. Last-observation ordering remains
deterministic when different native keys resolve to identical decoded pixels;
subsequent observations reuse the already calculated alias hash after validating
the retained pixels. There is no growing per-observation pending-list scan.
Alternate dimensions/pixel interpretations retain the original eager alias
behavior and observation ordering, without replacing existing mask/color data.
Clearing the cache clears both sources and index activation.

Matched diagnostic check after the repair: texture phase 10.41 ms, upload
7.56 ms, pipeline 5.29 ms, total draw CPU work 30.16 ms. Both runs execute 95
draws and upload 1,506,272 bytes of persistent geometry on frame 1. The texture
phase reduction is approximately 14 ms / 57%; it is not a whole-game FPS claim.
The final version with alternate-interpretation safeguards measured 15.84 ms
for textures, 12.61 ms for upload, 7.96 ms for pipeline and 46.30 ms draw CPU
total in a later diagnostic run. All phases varied; retain both results rather
than claiming that the earlier best component reduction is guaranteed.

Separate uninstrumented 3x300-frame experiment, zero warmup and the usual native
update accounting: prepared GPL first replay 58.30 / 36.01 / 30.98 ms; median
97.14 native FPS, median-run p99 19.875 ms, maximum 118.83 ms. Compare with the
preceding tranche's 62.00 / 36.75 / 41.00 ms, 100.33 FPS, p99 24.125 ms and
maximum 123.31 ms. These are mixed whole-frame results, not a general speedup.
Driver state and other phases remain variable. The final dimension-scoped
refinement affects alias queries, which the canonical timing path does not use.

Regression tests in `tools/renderer/tev_program/texture_source_cache_tests.cpp`
cover native masks/color before any alias request, repeated observations, two
different native keys with identical pixels, dimensional separation, additions
after an empty lookup, cache clearing and retained mask identity. The per-dimension
compatibility lookup can still incur hashing when a legacy consumer really asks
for it; this is deliberately NOT advertised as eliminating all grass-on stalls.
The local suite passes 14/14 tests; the final alternate-interpretation safeguards
also pass the focused cache test. Final Field and boot comparisons pass three
exact framebuffer matches each, zero Vulkan validation errors, zero runtime
shader compilation and zero shader-bearing libraries created after preparation.
Field frames also match the pre-change captures byte for byte. Linux/Android
hardware qualification and full default-effects performance remain open.

Private evidence: `%TEMP%/TriAevum-{replay-before,upload-before,alias-after,
replay-fixed-timing,replay-fixed-correctness,replay-final-diagnostics,
replay-final-correctness,replay-final-boot}-20260915`.
Next: isolate geometry allocation/upload and actual GPU execution. CPU staging
geometry currently feeds both NRI and scene-resource consumers; do not remove
those buffers just because the native draw owner is NRI. This requires a typed
resource-consumer/lifetime change, not skipping allocations behind a profile flag.

## First action on resumption

Prioritize ongoing inter-frame stalls at real-time x2/x3, with phase attribution
and the selected effect profile recorded. An uncapped 30 Hz simulation benchmark
does not establish smooth 60/90 Hz presentation. Keep cold-load and steady-state
results separate, without deleting the former from the report.
Then extend the actual startup/resource-preparation consumer to effect program families
and sample-count/alpha-coverage profiles using their declared interfaces. Do not
reintroduce shader inventories or prepare all combinations of material state.
Profile transitions and device recreation need explicit preparation boundaries
and ownership tests; they are not closed by the initial 1x preparation.
Attribute the remaining >100 ms frames separately now that the canonical test
proves no late shader-part creation. Preserve monolithic fallback and opt-in GPL
until platform and default-profile qualification pass. Linux/Android GPU tests,
M3 default effects and M4 transitions remain open.

## Real-time pacing and outline audit (2026-09-15)

Baseline for this tranche: `338454a`. `measure_pacing.py` now runs bounded,
real-time 30/60/90 Hz sessions, with VSync disabled, audio unchanged, no captures,
no collected shader pack and private copies of configuration/save data. It records
the completed-frame distribution and the worst 16 phase samples using the optional
`TRIAEVUM_PACING_TRACE=1` collector. `--warmup-frames` excludes completed
presentations, NOT elapsed seconds; its default is zero so loading stalls remain
visible. Frame timing describes host submission/presentation, not monitor scanout.
The legacy SDL target-FPS field is nominal on Vulkan: SDL skips its GL limiter.

Findings:

- Static native Field at 30 Hz: steady p99 33.875 ms, maximum 33.9309 ms.
  This is not evidence for all scenes, motion, interpolation or default effects.
- A historical x2 fixture with toon/outline and expensive grass settings spent
  4.696 seconds in source compilation. Its 180-frame warmup left only 43 measured
  frames in a 20-second run. That sample must not be described as 17 steady seconds.
- Historical x2 savestates also embed specialized shader sources in their visual
  replay. With effects disabled, their first replay still compiled 20 programs
  (about 2.0 seconds). This is a legacy snapshot issue, not evidence that newly
  submitted canonical draws require those variants. Do not rewrite their timing
  mode or silently discard saved visual state to make a test pass.
- Two clock-snapping prototypes did not consistently improve real x2/x3 results;
  both were removed. Simulation scheduling, interpolation and game speed are
  unchanged by the retained patch.

Retained repairs:

1. Parametric `fog_factor` is an unconditional shader-interface capability (value
   one when disabled). `GenerateOot3dPicaFragmentShader` formerly advertised it
   only when the first material enabled fog, despite caching the same program
   across both states. Its hook is now invariant. Generator tests exercise both
   activation states without changing the canonical shader source.
2. Offline outline preparation and runtime selection now share
   `ResolvePicaDrawInstrumentationFeatures`. In particular, transparent draws
   without depth writes do not request the fog/geometry outputs, and UI draws
   receive no scene-effect instrumentation. No duplicate eligibility policy.
3. The isolated toon artifact module includes finite output/coverage contracts
   and both vertex-color consumption contracts (340 toon artifacts, 396 including the
   existing native/temporal families). Texture identities, material values,
   dimensions and scene addresses do not create this family. Canonical rendering
   and the compatibility backend are unchanged; new outline binaries target NRI.
4. `build_fragment_artifacts --check` verifies source/interface identities and
   stored SPIR-V structure without recompiling every stored program. Developer
   `--reuse-builtins` regeneration may retain exactly matching built-in sources;
   `--check-exact` still recompiles all programs and rejects that shortcut.
   `--sources <directory>` exports maintained source for exact mismatch diagnosis.

Field toon+outline now passes three exact framebuffer comparisons against the
specialized reference, Vulkan validation, single NRI ownership and zero runtime
source compiler requests, with an empty application cache and no collected pack.
The intermediate test still had three source compilations / 1.400 seconds; after
the fog fix two / 0.892 seconds; after shared eligibility zero. These are scoped
compiler measurements, NOT a whole-game FPS improvement or cold-driver claim.

Rejected experiment: preparing all effect programs indiscriminately exceeded
the 120-second test timeout before gameplay. It and its enlarged library-retention
limit were removed. Do not reinstate this matrix as a startup solution. Next
device preparation must select the actual enabled profile and supported output
contracts, or reduce those programs through uniform data, with measured startup
cost. Driver library creation, legacy snapshot migration, grass resource work,
AO/reflection combinations and non-Windows hardware remain open.

Private evidence: `%TEMP%/TriAevum-pacing-{before,x2-before,canonical,
canonical-after,deadline-after}-20260915` and
`%TEMP%/TriAevum-outline-{final-check,fog-final-check,shared-final-check}-20260915`.
The `canonical-after` and `deadline-after` folders are rejected clock experiments,
not release candidates. Never use their timings as a claimed improvement.

### Ongoing intro stall reproduced and removed in the measured window

Booting normally, without a checkpoint or scenario injection, exposed another
stall about 18 seconds into the intro. It was not the initial loading interval:
the worst 60 Hz frame spent 647.36 ms in replay. Its missing fragment program
differed from the maintained offline family only in the typed toon response to
whether native TEV consumes primary vertex color. The builder now enumerates both
contracts for all eligible draw/effect combinations, not just the captured case.
No texture identity, scene exception or collected shader cache is involved.

Matched 24-second Windows/NRI runs, toon+outline, no grass, VSync off, audio on,
empty application cache, no captures or inventory in timed runs:

| Presentation | Before max / p99 (ms) | After max / p99 (ms) | After samples |
| --- | --- | --- | --- |
| 60 Hz | 649.53 / 19.75 | 52.90 / 17.25 | 1440 |
| 90 Hz | 418.40 / 14.625 | 51.75 / 14.75 | 2159 |

The 60 Hz source compiler cost fell from one request / 378.01 ms to zero;
the 90 Hz run also reports zero. The 90 Hz p99 did NOT improve. Peak reduction is
a paired-run observation, not a repeatability interval or cold-driver guarantee:
driver-internal caches were not cleared. In the final runs the largest replay
spikes occur in the first second (roughly 52 and 46 ms), while the previous
18-second stall no longer appears among the worst frames. Later samples include
present/wait time and occasional renderer frame-start waits, which need separate
attribution; they must not be called shader compilation without evidence.

Final correctness check: three exact Field framebuffer matches against the
specialized path, Vulkan validation, single NRI pipeline ownership and zero
source compilation. This does not validate every intro frame or every effect.
Generated sources are developer-built into the isolated artifact module; adding
the missing discrete contracts increases its generated C++ header to about
93 MiB. Do not broaden it with continuous material values or indiscriminate
startup pipeline preparation.

Evidence: `%TEMP%/TriAevum-outline-paced-boot-20260915` (before),
`TriAevum-outline-boot-source-20260915` (diagnostic inventory only),
`TriAevum-outline-closed-boot-20260915` (after), and
`TriAevum-outline-closed-field-20260915` (correctness).

Next priority: attribute the remaining present/frame-start waits with this same
bounded real-time harness, then replay transitions with the actual full default
effects profile. Keep Linux/Android validation and legacy saved replay sources
explicitly open. Do not substitute an unlimited average-FPS result for this work.

### Presentation wait and catch-up debt (2026-09-15, follow-up to 8c4b6b1)

`phase_timing.present` formerly combined GUI completion, the intentional software
pacing wait and actual end-frame submission/presentation. A nested `pacing_wait`
sample now exposes the wait independently, including in the bounded worst-frame
list. Do not subtract average wait time from an individual slow frame or sum these
overlapping phases.

The new measurements attribute several ongoing 20-30 ms frames primarily to the
pacer wait, not replay or source compilation. The final 30-second run spends
20.509 s of 20.956 s total present time waiting at 60 Hz, and 17.964 s of 18.621 s
at 90 Hz. Most of this is necessary rate limiting; the excessive individual waits
are still unresolved. These counters do not establish physical scanout timing or
identify the OS scheduler as the sole cause.

Fixed a mismatch between tested policy and the real pacer: the pure function's
two-period catch-up bound was overridden with a 250 ms window at runtime. At
90 Hz this admitted approximately 23 periods of obsolete presentation debt.
The actual caller now uses the tested two-period limit. A long stall rebases only
the presentation deadline, not elapsed time, guest simulation or audio clocks.
Short overruns still retain their phase; this is not a new interpolation algorithm.

`presentation_pacer_tests` exercises both policy boundaries at 30/60/90 Hz and
the actual pacer after an injected 60 ms stall. The latter would retain the debt
under the old runtime override despite the existing pure-policy tests passing.
It also checks that elapsed time is not reset and disabled pacing is inert.

Paired 30-second intro runs, same scoped toon/outline profile as above:

| Rate | Before max / p99 ms | After max / p99 ms | Carried debt before / after | Resyncs after |
| --- | --- | --- | --- | --- |
| 60 Hz | 50.47 / 17.50 | 61.18 / 17.125 | 19 / 9 | 1 |
| 90 Hz | 48.75 / 11.75 | 51.36 / 12.875 | 39 / 38 | 2 |

This is a bounded recovery correctness fix, NOT a measured overall performance
win: p99 worsened at 90 Hz and neither peak improved. Both final runs still have
zero shader source compilations. Timer wake jitter, initial replay work, default
effects and cross-platform qualification remain open.

Rejected experiment: submit GPU work before the pacing wait, retaining the wait
before presentation. It did not produce a stable benefit (90 Hz p99 reached
15.25 ms on repetition), so all scheduling/API changes and the experimental
helper were removed. One run also experienced multi-second CPU/unattributed
pauses while an unrelated Git command took tens of seconds; it is retained as
an anomalous observation, not proof of a renderer regression or a hardware limit.

Private evidence: `%TEMP%/TriAevum-submit-pacing-before`,
`TriAevum-submit-pacing-after` and `TriAevum-submit-pacing-repeat` (rejected
submission experiment), `TriAevum-bounded-debt-final` (retained implementation),
and `TriAevum-bounded-debt-field-check` (framebuffer correctness).

### Adult Link outside the castle: timed checkpoint qualification

Use `J:/TriAevum-verify-20260910/win-field-x2/checkpoint.oot3dsav`, SHA-256
`73a3d5e8ffe35a9048a07b909e50ced5cddb532b00a7b3ffe9d6c1156061af0d`.
The corresponding fixture framebuffer shows adult Link on foot facing the
castle-town bridge from Hyrule Field. This is NOT the mounted Epona fixture.
Run `measure_pacing.py` with the `win-field-x2/invocation.json`, this explicit
`--load-state`, `--no-scenario`, and no graphics-config override: Grass and toon
remain enabled. Preserve the original checkpoint and user configuration.

Baseline `6891f89`, 30 s, 60 Hz, no VSync/captures/inventory/collected shader pack:
frames 1 and 3 stall for 2795.9 and 7820.1 ms, almost entirely in replay. There
are 25 source compilations totaling 2376.5 ms; this is NOT a zero-compilation
checkpoint. Historical visual snapshots retain specialized shader sources.
Do not infer that all 25 requests or the entire replay delay have that cause:
remaining effect programs and driver creation must be classified separately.
After frame 180 the recorded worst samples instead peak at 28.0 ms, with the
largest wait-dominated sample spending 22.57 ms in the pacer.

The Windows host now optionally joins the OS MMCSS `Games` task while software
pacing is enabled, using the isolated `native_presentation_thread_scope.h` RAII
owner. It modifies only the calling thread's scheduling, not process priority,
registry values, GPU state or game timing. It loads the system `avrt.dll` only,
falls back normally if unavailable, unregisters when pacing is disabled or the
scope ends, and re-registers on reactivation. Non-Windows is an inert adapter;
unlimited throughput does not register. Runtime JSON exposes
`realtime_pacing.multimedia_scheduling_active`, rather than assuming success.
Reference: [Microsoft MMCSS task scheduling](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service).

Two same-checkpoint 40-second runs exclude the first 180 completed presentations
from the statistics (not a fixed number of seconds). Baseline here is the same
new binary with MMCSS attachment temporarily disabled, then rebuilt with the
final enabled implementation; no temporary switch remains in the source.

| State | Measured frames | Maximum ms | p99 upper ms | Mean ms |
| --- | --- | --- | --- | --- |
| MMCSS disabled | 1579 | 40.8883 | 17.125 | 16.66664 |
| MMCSS active | 1581 | 31.0742 | 17.000 | 16.66668 |

This paired observation is a modest steady-state improvement, not statistical
proof of universal benefit: the p99 difference is one histogram bucket. The
initial multi-second replay stalls remain essentially unchanged. All 15 focused
tests pass, including registration disable/re-enable and inactive unpaced mode.
No new framebuffer equivalence or Linux/Android performance claim for this patch;
canonical rendering, effects, density and resource scheduling were not modified.

Private evidence: `%TEMP%/TriAevum-castle-baseline`,
`TriAevum-castle-scheduling`, `TriAevum-castle-steady-control`, and
`TriAevum-castle-steady-final`. Next work must retain this fixture, distinguish
its legacy replay startup costs from ongoing waits, and extend the measurement
to camera movement before treating static-camera results as scene-wide closure.

### Castle checkpoint: Grass placement is the largest replay stall (2026-09-15)

The previous multi-second residual is now attributed, not assumed to be shader
compilation. `measure_pacing.py --diagnostics` enables bounded renderer phase
timers; the report retains enough frames to include startup. The diagnostics
buffer retains the LAST N frames, so a small fixed limit loses the critical
initial samples. Diagnostic runs are explicitly labelled and are not clean
performance measurements. `OOT3D_GRASS_DIAGNOSTICS=1` additionally enables the
existing per-surface extraction/clustering/world-conversion timers.

On the adult-Link castle checkpoint, one attributed replay takes 7688.7 ms:
7662.4 ms is `grass_render_ms`, while pipeline creation is zero and shader cache
lookup takes 0.017 ms. `InteractiveGrassPass::Prepare` waits for complete static
placement through `GrassStaticPlacementCache::Resolve`; the expensive operation
is CPU extraction, not the Grass draw call. The main surface generates
4,208,763 anchors and 269,527 clusters. Its baseline extraction takes 5887.5 ms,
clustering 721.6 ms, and world conversion 536.2 ms. Two smaller surfaces generate
384,482 and 84,329 anchors. IDs in private logs are evidence, never runtime keys
for special-case fixes.

Implemented in `grass_surface_extractor.cpp`:

- Replace 27 hashed 3D-cell lookups per candidate with nine X/Z-column lookups.
  The first point is inline; overflow points are sorted by Y cell, restricting
  comparisons to the exact same three vertical cells as before. Stacked floors
  do not require scanning all points in the column.
- Pool hash nodes within each extraction using a local standard C++ PMR pool.
  No shared allocator state, persistent cache, new dependency or renderer API.
- Preserve candidate quotas, random numbers, mask acceptance, strict spacing
  predicate, accepted order, density and every anchor attribute. Keep complete
  first-frame admission; do not remove the wait to hide the cost through pop-in.

Diagnostic main-surface extraction: 5887.5 -> 3861.8 ms (-34.4%). Counts of all
three surfaces and their clusters remain identical. A stronger hash experiment
did not improve this fixture and was discarded.

Clean 30-second runs, same checkpoint/full effects, empty application cache,
no shader pack, no VSync, no captures or detailed renderer/Grass diagnostics:

| Metric | Before | After |
| --- | ---: | ---: |
| Worst completed presentation | 7820.1 ms | 5733.0 ms |
| All-window mean (includes startup) | 25.90 ms | 23.30 ms |
| Completed presentations | 1159 | 1288 |
| All-window p99 upper bound | 19.125 ms | 24.875 ms |

The largest stall is reduced by 26.7%; it is NOT eliminated. The p99 did not
improve in this pair: do not claim a steady-state pacing improvement or infer
native simulation FPS from the 2x presentation count. The initial checkpoint
replay still compiles 25 compatibility shaders (2488 ms in the final run) and
two pass programs (333 ms). That separate problem remains open.

Verification:

- `grass_spacing_tests` checks 31,236 complete anchors in 36 cases against an
  independent brute-force filter, including negative coordinates, stacked
  floors, partial/black masks, three spacings, three randomness levels and
  nonuniform world transforms. All 16 focused tests pass.
- Rebuild the pre-change extractor from `b361f02` in the same runtime and compare
  deterministic framebuffer 120 with the final extractor. Identical BMP SHA-256:
  `3c23f514d880e168ee0cc5d1d9dfa9280243cdddb496f406878358fe3e631ed3`.
  Both use the same interpolated checkpoint mode, 121 presentations, throughput
  mode and fixed native step 1/30, not 1/60. These captures are correctness tests
  only. Full Grass/toon settings are preserved; the image shows adult Link at
  the bridge, terrain, Grass and HUD. No Windows screenshots were used.
- The final executable is rebuilt with the optimization, not the oracle.
  No AOT regeneration or shader artifact regeneration was required.

Private evidence under `%TEMP%`: `TriAevum-castle-full-attribution`,
`TriAevum-castle-grass-attribution`, `TriAevum-castle-column-pool`,
`TriAevum-castle-spacing-final-clean`, `TriAevum-spacing-capture-before` and
`TriAevum-spacing-capture-after3`. Capture arguments are retained with the files.

Next priorities, based on measured impact:

1. Further reduce static placement extraction/allocation work, then its
   clustering and world-conversion costs. Preserve mask/spacing semantics and
   first-frame completeness; the remaining five-second wait is unacceptable.
2. Replace the saved visual replay's compatibility shader-source dependency
   with the typed native-program path. Do not treat a warm cache as resolution.
3. Measure controlled camera movement after startup and distinguish new surface
   placement, simulation work, driver waits and software pacing. Static-camera
   tests do not establish scene-wide or Linux/Android performance closure.

### Active scope: frame flow AFTER preparation (2026-09-15)

The user has explicitly deferred preparatory work. The next work must target
interruptions during normal play, not the initial Grass build, replay restore,
startup compilation or an all-window average dominated by those costs. This
overrides the ordering of the three priorities immediately above.

Use `tools/renderer/tev_program/castle_field_motion.json` with the same adult-Link
checkpoint. The timeline waits 240 run frames, then moves Link away from the
bridge and laterally; it also supplies right-stick input, whose effect remains
subject to the existing TopScreen camera ownership/configuration. Do not assume
that the free-camera portion is active without checking it. Framebuffer 420
confirms actual movement, with Link running and a much wider grassy view.

The pacing harness now accepts `--input-timeline`, `--frame-start-diagnostics`
and `--guest-diagnostics`. Each is recorded in the result; runtime profiling
cannot accidentally remain enabled through a fixture flag. Diagnostic runs
are for attribution; clean runs omit these diagnostics and retain the timeline.

Measurements use 40 seconds, `--warmup-frames 180`, 60 Hz interpolated native30,
no VSync/fixed delta/captures, the full unchanged graphics profile and private
copies of configuration/save data. Statistics exclude the first 180 completed
presentations. They are presentation intervals, NOT native simulation FPS.

Attribution before optimization:

- A stationary run had an isolated 96 ms frame, with 90 ms in renderer frame
  start. Later frame-start tracing did not reproduce this late spike. Most
  other stationary outliers were pacing waits, not draw work.
- First movement produced 140-174 ms guest-dominated frames; a repeat with
  guest/service profiling did not reproduce them. Their cause is unresolved;
  do not claim a guest fix or assume hardware, paging or shader compilation.
- The reproducible moving-view problem is CPU Grass visibility/selection:
  approximately 19 ms per frame, versus ~0.05 ms placement and ~0.2 ms upload.
  All three placements are cache hits, static upload bytes are zero, pipeline
  creations are zero and shader variants are hits. This is per-frame work, not
  the preparatory bottleneck addressed in the preceding section.
- At frame 420, the largest surface visits 66,531 candidate clusters; cluster
  selection/order costs 9.956 ms and subsequent anchor evaluation 1.736 ms.

Implemented: `grass_cluster_order.h` provides bounded integer radix ordering
for large visibility lists, retaining comparison sort for small lists. Both
cluster-index lists and complete `GrassClusterWork` records use it. Original
ascending cluster order remains authoritative for budget admission; no camera
tolerance, stale visibility reuse, density reduction, pass reordering or native
shader change. Scratch storage is local and released by its owner; no hidden
thread-local state or new external dependency.

Paired clean test (new code, then a control rebuilt with ONLY the old sort):

| Post-warmup presentation metric | Old sort | New ordering |
| --- | ---: | ---: |
| Maximum | 36.3041 ms | 30.2499 ms |
| p99 upper bound | 30.875 ms | 27.375 ms |
| p95 upper bound | 20.625 ms | 18.375 ms |
| Mean | 17.1698 ms | 16.9644 ms |
| Frames exceeding 33.33 ms | 2 | 0 |

This is a targeted reduction of moving-view spikes, not a claim that all frame
pacing is solved. Average throughput improvement is small; the relevant result
is the tail, with p99 lower by 11.3% and maximum lower by 16.7% in this pair.

A second clean run of the final executable measured p99 29.25 ms, p95 20.875 ms,
mean 17.0944 ms, maximum 39.1415 ms, and four frames above 33.33 ms. Thus the p99
improvement repeated (5.3-11.3% across the two new-code runs), but the maximum
and p95 improvements did NOT. The 39 ms sample still contains 25.53 ms replay;
other outliers combine guest and replay work. Do not report the first pair as
proof of universal maximum-stall reduction or stutter-free gameplay.

Correctness: all 17 focused tests pass. Ordering tests compare 24 distributions against `std::sort`,
including empty/small/large lists, full 32-bit keys and work payload preservation.
Deterministic full-profile captures before/after at frames 420 and 460 are
byte-identical. SHA-256 respectively:
`342da12b4d456abc9ca7404f4cb59fd7aca80f78c0e3f772abcdf22471f58beb` and
`1b9c830329176b3d7e34f2ad70a995ca9e4968fdb31bb24ce5ddc4e0f9d74ba9`.
Capture runs use 461 presentations, fixed native step 1/30, throughput mode,
and the checkpoint's interpolated timing contract; never mix these with timing
results. The final runtime is rebuilt with the new ordering.

Private evidence in `%TEMP%`: `TriAevum-flow-control`,
`TriAevum-flow-motion-attribution`, `TriAevum-flow-guest-attribution`,
`TriAevum-flow-renderer-attribution`, `TriAevum-flow-selection-attribution`,
`TriAevum-flow-order-clean`, `TriAevum-flow-order-control-clean`, and
`TriAevum-flow-order-final-repeat`, plus
`TriAevum-flow-capture-before` / `TriAevum-flow-capture-after`.

Continue with the remaining moving-view visibility traversal/evaluation cost
and reproducible guest/frame-start/pacing spikes. Preparatory optimization is
deferred. No Linux/Android performance claim from these Windows measurements.

### Moving-frame visibility traversal: independent subtree jobs

Continue the post-preparation scope above. The radix-order change did not remove
the serial BVH traversal and LOD-prefix queries preceding parallel per-anchor
evaluation. These queries run again on camera movement and are not asset or
shader preparation.

Implementation:

- `GrassVisibilityQuery` shares the existing bound predicate between serial
  selection and `SplitGrassClusterSelection`. The splitter first applies the
  original ancestor culling, then emits disjoint subtree roots bounded by the
  requested job count. It does not approximate the frustum or reuse old views.
- `SelectGrassClusterSubtreeWork` traverses only its immutable subtree and
  writes caller-owned scratch. It preserves every retained-anchor prefix and
  individual-frustum-test flag. It performs no budget admission.
- `InteractiveGrassPass::Impl::SelectClusterWork` uses the already existing
  Grass worker pool for sufficiently large indexed surfaces. Small surfaces,
  unindexed surfaces and single-worker execution retain the serial path.
  All jobs finish before output is merged and radix-ordered. Existing density,
  budget admission and draw batching then operate on the same ordered list.
- Each task owns a separate output vector; immutable world/view inputs stay
  alive until every task has finished, including exception paths. Scratch is
  owned and cleared by the pass. No new thread pool, hidden global state,
  shader variant, delayed frame admission or preparation optimization.

Clean 40-second Windows/NRI runs, same full profile and movement timeline,
first 180 completed presentations excluded, native30 with 2x presentation,
VSync off, no fixed delta/captures/detailed diagnostics. The control was rebuilt
with only the call site restored to serial selection, retaining radix ordering.
The final call site and executable are restored to the parallel implementation.

| Post-warmup metric | Serial control | Parallel run 1 | Parallel repeat |
| --- | ---: | ---: | ---: |
| p99 upper bound | 28.0 ms | 18.0 ms | 25.0 ms |
| p95 upper bound | 19.875 ms | 16.875 ms | 18.625 ms |
| Maximum | 31.6301 ms | 31.0068 ms | 30.4187 ms |
| Mean | 17.0292 ms | 16.6667 ms | 16.8728 ms |
| Frames above 33.33 ms | 0 | 0 | 0 |

The p99 improvement is 10.7-35.7% in these runs, not a guaranteed 36%. Average
throughput changes little; do not present interpolated frame counts as native
simulation performance. Residual isolated ~30 ms frames remain. In the repeat,
some combine guest and replay work; others spend 17-21 ms in pacing waits.

A separate diagnostic run corroborates the owning phase: a matching work set
of 94,180 candidate clusters / 265,556 evaluated anchors / 252,741 visible blades
takes 11.532 ms selection and 12.973 ms total Grass CPU, versus 19.489 / 20.535 ms
in the earlier serial diagnostic observation. Both retain three placement hits,
zero static uploads and 12 draws. This is phase attribution, not a clean timing
pair; run frame IDs differ as real-time simulation reacts to missed deadlines.

Verification: all 18 focused tests pass. `grass_partition_tests` compares 90
serial/partitioned combinations, including view/distance changes, frustum
on/off, 1/2/3/10/64 jobs and reverse completion order, plus empty/zero-job cases.
Complete work records and retained-anchor totals are identical. Full-profile
framebuffer captures at 420 and 460 match the hashes in the previous section
byte-for-byte. No image, density or preparation trade-off was introduced.

Private evidence in `%TEMP%`: `TriAevum-flow-partition-clean`,
`TriAevum-flow-partition-control`, `TriAevum-flow-partition-repeat`,
`TriAevum-flow-partition-attribution`, and `TriAevum-flow-capture-partition`.
Next work remains post-preparation: residual moving-view CPU selection/merge,
guest/replay spikes and late presentation wakes. Do not claim all stuttering
resolved or extend the Windows performance result to Linux/Android without tests.

## Phase closure and future optimization handoff (2026-09-15)

The user considers this phase sufficient and requests no further optimization
in this phase. Preserve the implemented improvements; do not interpret this
closure as a rollback or as resolution of the outstanding causes.

Committed implementation checkpoints:

| Commit | Retained work |
| --- | --- |
| `b361f02` | Castle checkpoint pacing qualification and scoped Windows multimedia scheduling |
| `80ee461` | Exact pooled Grass spacing columns reducing preparatory placement cost |
| `33d241f` | Recurring Grass visibility radix ordering and moving-frame profiling harness |
| `d619428` | Parallel immutable visibility-subtree selection using the existing worker pool |

The latest post-preparation Windows comparison is the table above: p99 falls
from 28 ms serial to 18-25 ms in two parallel runs, with isolated maxima still
around 30 ms. These are paced 60 Hz presentation intervals over native30 with
2x interpolation, not unlocked native simulation throughput. Eighteen focused
tests pass, and deterministic framebuffer captures at 420/460 are byte-identical
to the serial reference. No density, image-quality or gameplay-speed concession
was used. No Linux/Android performance conclusion follows from these tests.

Revisit in a future explicitly requested optimization phase:

1. Remaining per-frame Grass visibility traversal, result merge and evaluation
   cost on wide moving views; preserve exact masks, admission and draw order.
2. Late pacing wakes and guest/replay/frame-start spikes. The earlier isolated
   140-174 ms guest and ~90 ms frame-start observations remain unexplained and
   not reliably reproduced; do not classify them as fixed or assume a cause.
3. Independently attributed upload, allocation, synchronization and GPU costs,
   ranked by their contribution to bad frames rather than average FPS alone.
4. Full supported-profile program coverage and pipeline lifetime tests across
   AA/effect/resolution/fullscreen/scene transitions, plus Linux/Android checks.
5. Separately deferred preparation work: initial Grass construction and the
   saved-replay compatibility shader path / remaining device preparation.

Resume from this implementation, not from a fresh renderer migration. Reuse
`tools/renderer/tev_program/measure_pacing.py` and `castle_field_motion.json`
with the adult-Link castle checkpoint documented above. Re-establish a clean
baseline on the then-current build; keep startup, diagnostic and deterministic
capture runs separate from normal-flow timing. Temporary private evidence may
expire, so the commands, fixture identity, hashes and measured limits recorded
here are the handoff, not an assumption that `%TEMP%` remains available.

Do not restart this backlog automatically during unrelated feature work.
