# Structural stuttering resolution

Date: 2026-09-15. Baseline: `a9d9441`, branch `port/linux-nri`.
Status: executable strategy; the remaining implementation is NOT complete.

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

Prioritize the measured first-replay and GPU stalls above. Then extend the actual startup/resource-preparation consumer to effect program families
and sample-count/alpha-coverage profiles using their declared interfaces. Do not
reintroduce shader inventories or prepare all combinations of material state.
Profile transitions and device recreation need explicit preparation boundaries
and ownership tests; they are not closed by the initial 1x preparation.
Attribute the remaining >100 ms frames separately now that the canonical test
proves no late shader-part creation. Preserve monolithic fallback and opt-in GPL
until platform and default-profile qualification pass. Linux/Android GPU tests,
M3 default effects and M4 transitions remain open.
