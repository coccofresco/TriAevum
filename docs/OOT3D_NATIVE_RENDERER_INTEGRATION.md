# OoT3D native renderer integration

## Objective

Port and own the renderer developed in `I:\oot3d-native-renderer` while
progressively removing every dependency of `oot3d_native_game` on the legacy
game runtime, Fast3D compatibility, and OoT N64 data or rendering semantics.
Reusable ThreeDsRecomp platform and renderer services remain the supported
runtime boundary.

The migration is incremental. Each step must reduce or preserve the measured
dependency boundary, build from the dedicated worktree, and pass the smallest
test set that covers the changed contract. A broad rewrite is not accepted as
a substitute for a verified migration.

## Dedicated branches and worktrees

- Parent branch: `renderer/oot3d-native-integration`
- Parent worktree:
  `I:\oot3dre_work\oot3d-native-renderer-integration`
- Renderer integration branch:
  `integration/oot3dre-native-game`
- Renderer source branch: `oot3d-vulkan-renderer`
- Renderer source repository: `I:\oot3d-native-renderer`
- Dedicated build tree:
  `I:\oot3dre_work\build-native-renderer-integration-v2`

The parent starts at project commit `71a99e502`. The renderer import starts at
`ffa3a1fa`. Commit `5bce42df` updates the Vulkan diagnostics test to the current
alpha-test and alpha-to-coverage contract. The older project-side scanout fix
`e22a89e5` is not replayed: the imported renderer already preserves the last
actually presented transfer, retains it across presentation-only recreation,
and clears it on native scene reset.

Project commit `0f79f2c96` also closes the TopScreen IPS record 27 control-flow
contract at its original observable `BEQ` successor. This allows a reduced A32
build to execute the source port without relying on an unrelated mass-AOT
region to cross an unmaterialized padding instruction.

Renderer commit `89ba6b70` completes the first ownership extraction. The
graphics settings runtime now consumes a host-neutral JSON persistence port,
and the renderer-owned ImGui code is a panel delegated by the current host
window adapter. Renderer core files no longer include or name Ship, N64 or
Fast3D interfaces.

Renderer commit `f6e85252` moves SDL borderless/fullscreen policy behind the
window adapter. The Vulkan backend no longer reaches `Ship::Context` or CVars;
its sole remaining Ship file reference is the native OoT3D texture decoder,
which is scheduled for domain extraction rather than hidden by a host wrapper.

Renderer commit `6050bcb8` completes that extraction. A host-neutral decoder
now owns all 14 native PICA texture formats, including tiled 4bpp, ETC1 and
ETC1A4. The old symbol forwards for consumers not yet migrated, while Vulkan
depends directly on the renderer module. The post-extraction Kokiri capture is
pixel-identical to the pre-extraction gate (`8B0D179E...23781AC`).

Renderer commit `da2f8e26` adds one host-neutral settings-tab extension point.
The application uses it for TopScreen 1.2 without introducing game or
TopScreen types into renderer core. The application tab edits the existing
typed external-JSON store; successful writes advance one runtime revision
consumed by every HUD and camera owner on the next frame.

The native PICA submission boundary is now renderer-owned as
`Oot3d::Renderer::PicaRenderBackend`. The application bridge depends only on
this interface and `oot3d_renderer_contracts`; it no longer includes Fast3D or
links ThreeDsRecomp. `Fast::GfxRenderingAPI` temporarily implements the
interface and retains aliases for consumers that have not migrated yet. This
removes a real CMake edge without introducing a runtime conversion layer or
copying native vertex, index, texture, uniform or transfer payloads.

The native PICA texture decoder is also an independently linkable renderer
target. The UI texture provider calls `Oot3d::Renderer` directly, while the
old Fast3D name is an inline source-compatibility wrapper. Native CTXB payloads
continue to be decoded from guest memory without Ship resource services.

The Room Compilation Unit runtime has no Ship or renderer dependency. Its
former ThreeDsRecomp link only supplied the JSON parser transitively and is now
an explicit `nlohmann_json` dependency.

The native OoT3D asset parsers and providers are now built as the autonomous
`oot3d_native_asset_runtime` target. Its 19 sources contain no Fast3D or Ship
host-service references. `NativeSourceProvider` accepts immutable byte buffers
through an injected loader; the two legacy host call sites convert archive
files at their existing application boundary. The native gameplay runtime
therefore consumes the asset runtime directly and no longer links
ThreeDsRecomp.

The N64-layout/TopScreen primitive renderer now consumes the renderer-owned
`UiRenderBackend` contract and an injected texture provider. Its core no
longer includes Fast3D, Ship context or resource services. The old Fast3D
shader path and OTR texture resolver are isolated together in
`oot3d_ui_legacy_adapters`; replacing that adapter with the native 2D backend
and OoT3D-only texture sources is the final direct ThreeDsRecomp link step.

## Current measured boundary

Run:

```powershell
python tools\oot3d\renderer_integration\audit_renderer_dependencies.py `
  --repo-root . `
  --policy tools\oot3d\renderer_integration\renderer_dependency_policy.json `
  --output renderer_dependency_audit.json
```

The current ratchet is:

| Scope | Files | Legacy runtime API | N64 files | Fast3D adapter files |
| --- | ---: | ---: | ---: | ---: |
| Renderer core | 275 | 0 | 0 | 0 |
| Vulkan backend | 9 | 0 | 1 | 2 |
| Native renderer adapter | 42 | 0 | 1 | 4 |
| Native game frontend | 404 | 8 | 1 | 10 |

The audit resolves CMake alias targets and includes `tools/oot3d/ui_n64`.
Without those two rules it incorrectly omitted the
`oot3d_native_runtime_host -> oot3d_native_cutscene_host` edge and the
remaining UI adapter sources. The corrected `oot3d_native_game` closure
reaches `three_ds_recomp_runtime` through one target:

- `oot3d_native_application_host`

All limits are upper bounds and may only decrease. New native code must keep
the legacy-runtime count at zero in renderer scopes; the eight frontend
adapters are the explicitly measured migration boundary. The single CMake edge
is the supported application-to-runtime dependency.

The PICA boundary extraction is qualified by the isolated
`oot3d_native_pica_vulkan_bridge_tests`, a complete ThreeDsRecomp and native
game build, and a deterministic 120-frame Vulkan run from
`hudtest_after120.oot3dsav`. Its delayed framebuffer SHA-256 is
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`,
exactly matching the pre-extraction capture.

The texture-provider extraction additionally passes
`oot3d_native_ui_texture_provider_tests` and all 157 renderer foundation tests.
Its deterministic Vulkan framebuffer has the same
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`
SHA-256 as both sides of the PICA backend extraction.

The RCU dependency correction passes
`oot3d_room_compilation_runtime_tests`, the complete native game link and the
dependency audit. It changes no renderer or scene-composition code.

The asset-runtime extraction passes the native gameplay and actor runtime
tests, 152 applicable OoT3D ThreeDsRecomp tests, all 157 renderer foundation
tests, the complete native-game link and the dependency audit. One unrelated
legacy resource-contract fixture still expects a manifest without the
`source_cmb` field that the existing implementation requires; it is excluded
from this migration gate rather than weakening the native source contract.
The delayed Vulkan framebuffer remains exactly
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.

The UI backend extraction passes `oot3d_n64_ui_renderer_tests`,
`oot3d_native_ui_texture_provider_tests`, the complete native-game build and
the dependency audit. Its delayed TopScreen Vulkan framebuffer is also exactly
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.

## Migration order

### 1. Import and qualification

Keep the imported renderer history intact, apply only integration fixes, and
qualify its core contracts before changing ownership. Required evidence:

- `oot3d_vulkan_diagnostics_tests`
- `oot3d_graphics_foundation_tests`
- complete `three_ds_recomp_runtime` build
- native game build and Vulkan framebuffer smoke
- normal and Restoration TopScreen profiles remain isolated and functional

The qualified native-game build must use the same gameplay closure as the
baseline. A renderer comparison made with mass/whole AOT absent is invalid:
the reduced runtime follows different dispatch paths and does not measure
renderer parity. The current qualified closure reports:

- 138,850 mass-AOT entries
- 73,232 compiled-function entries
- 1,552,722 whole-AOT calls in the 180-frame Kokiri gate
- zero retained ARM fallbacks
- 41,619 native PICA draws and 718 display transfers

### 2. Remove Ship from the renderer core (complete)

The first extraction is deliberately narrow:

1. Replace direct `Ship::Context` configuration access with an injected
   graphics-settings store.
2. Move the `Ship::GuiWindow` implementation behind a host adapter. The
   renderer exposes only settings state and commands.
3. Lower the renderer-core Ship ratchet from two files to zero.

This leaves the GPU implementation unchanged while making its control plane
host-independent. The replacement is covered by an in-memory load/store test,
the complete 142-test graphics foundation suite, Vulkan diagnostics, the
TopScreen profile test, the full native-game build, and a delayed 1280x720
Kokiri framebuffer capture with 4543 sampled colors.

### 3. Own OoT3D render data outside Ship

Move the native CMB/CMAB/CSAB/CTXB render model, scene submission types and
resource contracts from `ThreeDsRecomp::Oot3d` into an OoT3D-owned target and namespace.
Keep temporary forwarding aliases only while consumers migrate. No N64
display-list type may enter the new public interface.

### 4. Replace the Fast3D adapter

`Oot3dNativeFast3dRenderer` is a compatibility adapter and the only native
renderer file that includes `fast/lus_gbi.h`. Replace it with a backend-neutral
OoT3D command/submission contract consumed directly by the Vulkan renderer.
PICA state, TEV, lighting, fog, blend, depth, texture and transfer semantics
remain native OoT3D data.

### 5. Extract host services

Replace remaining ThreeDsRecomp services with narrow application-owned ports:

- window and Vulkan surface
- input
- audio device
- logging
- JSON configuration
- filesystem/archive access
- framebuffer capture and diagnostics

Adapters may initially call ThreeDsRecomp, but core and gameplay targets must
depend only on the port interfaces. Each adapter is then replaced independently.

### 6. Remove OoT N64 and legacy-game code from the native-game closure

Migrate the five measured CMake edges one at a time. UI behavior that must be
preserved is reimplemented against OoT3D assets and application-owned state;
it is not retained by linking legacy gameplay or N64 render data. When the
audit reaches zero, configure a native-only product build with only the
ThreeDsRecomp runtime dependency.

## Priorities after renderer import

Once the imported renderer is stable behind the existing OoT3D contracts, work
continues in this order:

1. Profile and remove in-game CPU/GPU stalls without reducing OoT3D draw,
   transfer, audio or gameplay coverage.
2. TopScreen 1.2 configurator integration is complete. Its dedicated tab edits
   and atomically persists the same external JSON contract; gameplay button
   chords and guest save data remain out of the preference path. The direct
   PICA host drives the ThreeDsRecomp ImGui overlay explicitly, and `F1`
   toggles the combined renderer/TopScreen settings window.
3. Qualify and expose the advanced renderer features already present in the
   imported branch. Each feature must retain an authentic/off path and pass
   its capability, structural and framebuffer gates before becoming a user
   option.

The JSON configuration remains the persistence authority while the
configuration UI is introduced. UI state must not become a second settings
store.

## In-game performance baseline

A 300-presentation-frame Vulkan profile from `hudtest_after120.oot3dsav`
separates renderer cost from guest execution. With both mass- and whole-AOT
enabled, 599 guest refreshes take 17.85 seconds of host-loop time (16.81 host
fps); 12.36 seconds are guest execution and 11.03 seconds are compiled
dispatch. PICA submit plus backend account for 4.72 seconds and visual
presentation for 2.66 seconds.

The same run with mass-AOT disabled retains 599 refreshes and zero ARM
fallbacks, but reduces host-loop time to 14.21 seconds (21.11 host fps), guest
time to 8.95 seconds and compiled dispatch to 7.67 seconds. Mass-AOT is
therefore disabled by default while whole-AOT remains active;
`--enable-mass-aot` is retained for diagnostics. The framebuffer remains
exactly `305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.

Texture snapshotting compared 1.12 GB but copied only 1.42 MB in the slower
run, consuming 0.13 seconds. It is measurable but not the primary blocker.
The next performance work must reduce whole-AOT/direct-call fragmentation or
replace hot closure regions with typed C++; lowering renderer coverage would
not address the dominant cost.

### Recovered LLVM baseline and historical comparison

The earlier performance result was not produced by the materialized MSVC
archive used by `build-native-renderer-integration-v2`. Git history and the
preserved profiles identify three cumulative milestones:

- the complete MSVC whole-AOT path reached 62.28 FPS on the original 30 Hz
  Kokiri workload;
- Clang ThinLTO, PGO and binary block-hook lookup reached a 78.33 FPS median;
- removing diagnostic PICA history, reusing submission storage and consuming
  native draw resources without copies reached 122.6 steady refresh/s in the
  4K title workload.

The MSVC integration build was still linked to
`whole_aot_prelinked_shiftfix/oot3d_native_whole_aot_shiftfix.lib`. A
300-presentation run took 10.5655 seconds in the host loop and 7.2600 seconds
in guest execution. Because it missed the 60 Hz deadline, the scheduler
performed 599 guest updates for 300 presentations and nearly doubled the
render workload to 69,459 draws.

The current source was rebuilt with Clang 22.1.6 and the coherent PICA external
archive:

```text
I:\oot3dre_work\whole_aot_thinlto_shiftfix\
  oot3d_native_whole_aot_pica_external_coherent_thinlto.lib
```

The isolated consumer is:

```text
F:\oot3dre_build\native-renderer-llvm\oot3d_native_game.exe
```

On the same checkpoint, a fixed-delta 300-frame TopScreen run performs exactly
300 guest updates and 34,775 draws. It records 5.0217 seconds host-loop time
(59.74 FPS), 2.6368 seconds guest, 1.2436 seconds PICA submit and 0.9150 seconds
PICA backend, with zero unsupported exits or memory faults. The evidence is
`F:\oot3dre_build\native-renderer-llvm\profile_fixed_300f.json`.

The remaining gap from the historical 78 FPS is primarily a workload change,
not an AOT regression. A true 60 Hz simulation experiment on
`navi_kokiri_main_forest.oot3dsav` executes 480 game updates and 78,800 draws
for 480 presentations, reaching 57.82 FPS. The historical profile contains
37,817 draws. Restoring the native 30 Hz simulation produces 240 updates and
37,578 draws; guest time falls below the historical value, while visual
interpolation costs 2.3600 seconds and keeps presentation at 59.33 FPS.

The production default is therefore native 30 Hz gameplay timing presented at
60 Hz through visual interpolation. True 60 Hz simulation remains an explicit
diagnostic mode because changing the guest update rate accelerates gameplay
and can invalidate interaction timing.

Consequently, do not try to recover the old result through more scalar A32
wrappers or by reordering true-AOT ahead of whole-AOT. The next production
work is:

1. keep the coherent Clang ThinLTO consumer as the performance baseline;
2. reduce visual interpolation cost without advancing gameplay state;
3. optimize repeated PICA submission independently of the simulation rate;
4. compare fixed update counts, not presentation counts from a run that has
   entered scheduler catch-up.

## Test policy

Use tests according to the changed boundary:

- Pure renderer algorithms: foundation tests only.
- Vulkan ownership, synchronization or diagnostics: diagnostics plus the
  relevant renderer test and validation smoke.
- Renderer/backend ABI: complete renderer build plus native game build.
- Scene composition, material or UI boundary: delayed framebuffer captures
  from a real Kokiri checkpoint for the built-in and TopScreen profiles.
- Removal of a dependency: dependency audit plus the tests of its replacement.

Framebuffer captures must come from the renderer, not from the Windows desktop.
No screenshot comparison may replace structural validation of PICA state,
draw ownership, target lifetime or the dependency audit.

The first presentation frame after loading a native checkpoint is expected to
be empty. Single-frame smoke captures therefore use
`--screenshot-start-frame 179` with `--frames 180`; sequence captures have
verified valid output from frame 30 onward. Treating the default frame-zero
capture as renderer output creates a false black-frame failure.

## Build environment

The build must run inside the Visual Studio 2022 developer shell. The dedicated
tree uses the already qualified vcpkg checkout through:

```text
I:\oot3dre_work\build-native-renderer-integration-v2\vcpkg
  -> I:\oot3d-native-renderer\.renderer-dev\build\vcpkg
```

Configure renderer tests with `THREE_DS_RECOMP_BUILD_TESTS=ON`. Running CMake outside the
developer shell can find `cl.exe` while still failing to find standard C++
headers, so a bare `cmake --build` is not a valid reproduction command.

The dedicated build currently reuses the qualified gameplay closure:

```text
OOT3D_MASS_AOT_GENERATED_DIR =
  I:\oot3dre_work\mass_cpp_9fd2f56\artifacts
OOT3D_MASS_AOT_PREBUILT_LIBRARY =
  I:\oot3dre_work\mass_cpp_9fd2f56\build-native-corpus3\oot3d-a32-mass-aot.lib
OOT3D_WHOLE_AOT_GENERATED_DIR =
  I:\oot3dre_work\whole_aot_optimization\scalar_full
OOT3D_WHOLE_AOT_PREBUILT_LIBRARY =
  I:\oot3dre_work\whole_aot_prelinked_shiftfix\oot3d_native_whole_aot_shiftfix.lib
```

These are temporary parity inputs, not desired final dependencies. They remain
until their source-recompiled C++ replacements cover the same gameplay paths.

## Corrected ThinLTO performance baseline

The materialized shift-fix archive above is semantically correct but prevents
whole-AOT ThinLTO. A fixed-delta, 120-presentation-frame run from
`hudtest_after120.oot3dsav` measured 27.26 FPS, with 2.74 seconds in the guest
phase and 2.19 seconds in sampled A32 dispatch.

A corrected Clang 22 ThinLTO/PGO archive was produced by replacing the 126
shards affected by register-specified ARM shifts in the previous profiled
bitcode archive. It contains 257 unique members and has SHA-256
`1B2A06111FAEED2A747159C9DCCB5C7582CFA16C0E956DEBADC347FAEFEBFD85`.
The Clang consumer must link `clang_rt.builtins-x86_64.lib` even when whole-AOT
arrives through `OOT3D_WHOLE_AOT_PREBUILT_LIBRARY`; renderer dependencies also
retain warnings without treating pinned NRI/FFX Clang diagnostics as errors.

The equivalent corrected run is recorded in
`I:\oot3dre_work\native_game\native_renderer_perf_thinlto_shiftfix_120_20260723.json`.
It reaches 31.89 FPS, reduces the guest phase to 1.66 seconds and sampled A32
dispatch to 1.09 seconds, with zero retained ARM fallbacks and zero
whole/mass-AOT unsupported exits. This is a real improvement, but not a closed
performance result: PICA submit takes 1.77 seconds, including 1.36 seconds in
the backend, so renderer submission is now the primary measured limit.

Delayed framebuffer evidence is
`I:\oot3dre_work\native_game\native_renderer_thinlto_shiftfix_visual_20260723.bmp`
with SHA-256
`6AF56D41F65AF61B3C87AD833A9FC8EC0ECB68744B0596B9AD4F0104D74C65D2`.
The validator reports 1280x720, 4,958 sampled colors, full channel range and a
0.9916 chromatic fraction. The default frame-zero screenshot is black because
the loaded checkpoint has not presented a valid frame yet; it is not renderer
evidence.

## Native PICA CPU profile and zero-copy uploads

Opt-in per-frame diagnostics now split native PICA CPU submission into shader
variants, shader cache, texture handling, draw state, pipeline lookup, upload,
descriptor and command phases. On the corrected ThinLTO build, 13,895 draws
over 120 frames attributed 0.677 seconds to NRI command submission, 0.281
seconds to texture handling and 0.160 seconds to upload preparation.

All measured draws were already NRI-owned, but the Vulkan interop path copied
about 200.6 MB of vertex, index and uniform ranges into a second committed NRI
arena. The default now wraps the existing host-mapped Vulkan arenas directly.
`OOT3D_GRAPHICS_NRI_PICA_UPLOADS=1` retains the isolated copied-arena path for
diagnostics. This changes ownership of no draw, descriptor, shader, pipeline
or texture.

Equivalent fixed-delta runs with diagnostics disabled measured:

- copied NRI arena: 36.60 FPS, 1.19 seconds in the PICA backend;
- zero-copy default: 44.12 FPS, 0.64 seconds in the PICA backend.

The delayed framebuffer captures are byte-identical. The zero-copy capture is
`I:\oot3dre_work\native_game\native_renderer_zero_copy_default_visual_20260723.bmp`
with SHA-256
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.
The texture consumer now keeps immutable source and transformed snapshots
keyed by complete native PICA texture state. Every reuse first compares the
current guest-memory payload byte-for-byte; changed data creates a new
snapshot and content hash, so animated and runtime-modified textures retain
their original behavior. The content hash travels with the renderer texture
view and is no longer recomputed in the Vulkan backend.

The same 120-frame checkpoint records 13,829 cache hits and 66 misses. It
compares the same 224,809,984 logical bytes but copies only 1,401,728 bytes.
Texture capture falls from 0.283 to 0.028 seconds and backend texture handling
from 0.279 to 0.035 seconds. With diagnostics disabled the run reaches 50.03
FPS, with 0.538 seconds in PICA submit and 0.402 seconds in the backend.

The capture
`I:\oot3dre_work\native_game\native_renderer_texture_cache_nodiag_visual_20260723.bmp`
remains byte-identical, with SHA-256
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.
The submission and Vulkan-plan tests also verify cache reuse followed by
invalidation when one native texture byte changes.

## Typed mesh command submission

The first residual whole-AOT rendering hotspot is now a typed C++ consumer.
`MeshCommandPacket_Submit` (`0x00466E2C`) preserves the recovered native
contract: field `+0x14` selects the active packet slot in four-byte units,
field `+0x10` supplies the byte count, the selected slot supplies the command
source, and `0x0054CC4C` is both the destination cursor and byte-accounting
state. The implementation validates every source and destination range before
its first write and retains the existing AOT path for malformed or unsupported
packets.

The unit test executes the original generated A32 function and the typed
consumer from cloned guest memory, then compares the command payload, cursor,
return ABI and write behavior. In the fixed 120-frame Kokiri checkpoint the
typed path handles 9,694 submissions with zero retained fallbacks. The former
hottest inner loop at `0x00466E7C`, previously estimated at 1,452,352 entries,
is absent from the new hot-block frontier. The next measured boundaries are
`BgCheck_LineTestImpl` and `PicaCommandWriter_WriteRegisterRange`.

The no-diagnostics run changed 50.03 FPS to 51.36 FPS in this sample. Treat
that timing as directional rather than a broad gameplay claim; the stronger
acceptance evidence is the removed A32 loop and the byte-identical delayed
framebuffer. Both captures have SHA-256
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.
The current reports are:

- `I:\oot3dre_work\native_game\native_renderer_perf_typed_mesh_submit_active_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_a32_blocks_typed_mesh_submit_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_typed_mesh_submit_active_visual_20260724.bmp`

Do not add arbitrary runtime-only observable exits to the current prebuilt
whole-AOT archive. A focused attempt at
`PicaCommandWriter_WriteRegisterRange` showed that its promoted registers are
not materialized at that newly introduced boundary; treating the stale host
state as its AAPCS entry corrupts the command writer. That function can be
promoted only by regenerating its whole-AOT region with the exit declared at
lowering time, or by replacing the containing typed caller. The ineffective
runtime probe and observable exit were removed after the real checkpoint
showed that they never reached a valid function-entry ABI.

## PICA register writer whole-AOT boundary

`PicaCommandWriter_WriteRegisterRange` (`0x00307BD8`) now uses the existing
validated host implementation through a lowering-time whole-AOT external
boundary. The selection manifest no longer emits the function as generated
A32; its 17 generated callers materialize architectural state and call
`Oot3dAotCallExternal` instead. This is a compile-time ABI decision, not a
runtime address or register heuristic.

While regenerating that selection, the program builder exposed a general CFG
bug: conditional returns such as `BXLT LR` emitted the return edge but omitted
the non-taken fallthrough. The builder now preserves both successors for
conditional indirect returns and other conditional instructions that write
the program counter. A synthetic regression test verifies that the
fallthrough remains in the owning function and no residue function is
invented. On the recovered game inventory this folds the former
`cfg_residue_00307BE0` into its real PICA writer owner.

The coherent Clang 22 ThinLTO archive is:

```text
I:\oot3dre_work\whole_aot_thinlto_shiftfix\
  oot3d_native_whole_aot_pica_external_coherent_thinlto.lib
```

It was rebuilt from one internally consistent 256-shard generation. Incremental
generation now records each shard's function entrypoints in the structured
manifest instead of recovering ownership only from generated C++ text. The
legacy parser also accepts both pre- and post-architectural-state-promotion
signatures. Removing and restoring `0x0047AF24` consequently changes two
dependent shards rather than shifting 251 shards. Do not mix selectively
regenerated shards with objects from before this ownership baseline.

Also link whole-AOT consumers one at a time; linking the game and test
executables concurrently makes two ThinLTO jobs compete for the same cache and
can stall the machine. A changed 1.15 GB ThinLTO archive may still make the
first consumer link expensive even when only a few shards changed; subsequent
links reuse the ThinLTO cache.

In the fixed 120-frame checkpoint, the host writer handles 63,188 calls with
zero retained fallbacks. Whole-AOT entries fall from 689,774 to 532,824 and
external calls rise from 264,291 to 327,479. Guest time changes from 1.203 to
1.144 seconds in this directional sample. A 306,322-sample block profile no
longer contains `0x00307C44`, and the delayed framebuffer remains byte
identical with SHA-256
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.
Evidence:

- `I:\oot3dre_work\native_game\native_renderer_perf_pica_external_final_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_pica_external_profile_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_pica_external_final_20260724.bmp`

`ActorTrackingStateReset` (`0x0047AF24`) was tested as a second external
boundary and deliberately rejected. Its host implementation was
ARM-differentially correct and removed the expected loop blocks, but it added
7,901 high-frequency external transitions. The available actor-boundary run
had block profiling enabled, so its roughly 1.65-second guest time cannot be
compared to the 1.14-second non-profiled PICA baseline; it did not establish a
release-mode gain. The function therefore remains inside whole-AOT rather than
committing an unproven boundary. Future typed extraction should prefer larger
callers/regions that amortize one state flush across substantial work rather
than leaf functions selected only by block-entry count.

Stable-shard baseline evidence:

- `I:\oot3dre_work\native_game\native_renderer_perf_pica_stable_shards_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_perf_pica_stable_shards_final_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_pica_stable_shards_final_20260724.bmp`

## Whole-AOT external-call profile and matrix fast path

The 300-frame `hudtest_after120` profile now records the hottest whole-AOT
external destinations only when A32 runtime profiling is enabled. The
diagnostic uses a dense code-address counter and 1/64 timing samples; normal
runs allocate no counter table and execute no timing probes.

The profile showed that 1,216,617 of 1,319,108 external calls were already
typed helpers rather than missing guest code:

- 833,930 calls to `Mtx3x4_Multiply` (`0x0036C174`);
- 350,940 calls to `Mtx3x4_CopyIfDistinct` (`0x00372224`);
- 31,747 calls to `memcpy` (`0x00371738`).

`Mtx3x4_Multiply` alone accounted for an estimated 1.669 seconds. Its ordinary
finite, round-to-nearest inputs now use host IEEE single-precision operations
with explicit non-fused rounding between multiply and add. Non-default
rounding, enabled VFP exceptions, subnormal, infinite or NaN operands/results
retain the complete scalar VFP implementation. Profiling additionally compares
the first 4,096 fast results, all twelve output words and FPSCR exception flags,
against that implementation.

On the same 300-frame profile, 833,930 calls used the fast path with no soft
fallback, retained ARM fallback, value mismatch or flag mismatch. Guest time
fell from 8.7790 to 7.2327 seconds (17.6%), while sampled dispatch time fell
from 7.5206 to 5.9755 seconds (20.5%). The asynchronous framebuffer sequence
at frame 119 remains byte-identical with SHA-256
`305D5EC012BEDED5349EAFF8C5CF796BE116F7721664F04E9283CA1DB9169AD9`.

Evidence:

- `I:\oot3dre_work\native_game\native_renderer_profile_300f_external_timing_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_profile_300f_fast_mtx_final_20260724.json`
- `I:\oot3dre_work\native_game\native_renderer_fast_mtx_sequence_20260724_000119.bmp`

## Native-only UI texture path

The native game no longer constructs an N64 archive texture provider behind
the OoT3D UI texture provider. TopScreen and the native UI renderer now resolve
their texture identities exclusively from guest OoT3D CTXB/PICA data and the
optional OoT3D override pack. The removed compatibility path included Ship
resource/archive services, N64 font lookup, and N64 texture format decoding.
Sampler state now uses the renderer's native sampler contract rather than N64
GBI flags.

The corrected dependency audit decreases the native frontend ratchet from 111
to 110 Ship-dependent files and from two to one N64-dependent file. It still
reports two honest reachable CMake edges: the Fast UI rendering adapter and
the oversized legacy cutscene host. This change removes content fallback, not
either remaining link edge.

A deterministic Vulkan run from `hudtest_after120.oot3dsav` opened the Items
page through the original input timeline. It rendered 186 host primitives,
resolved one native texture with zero failures, uploaded it once, and served
185 cache hits. The framebuffer preserves the prior Items-page composition;
the complete-frame RMSE against the earlier reference is 0.0128262, including
the independently advancing 3D background.

Evidence:

- `I:\oot3dre_work\native_game\renderer_dependency_audit_ui_fallback_removed_20260724.json`
- `I:\oot3dre_work\native_game\renderer_ui_fallback_removed_20260724.json`
- `I:\oot3dre_work\native_game\renderer_ui_fallback_removed_20260724.bmp`

The remaining concrete Fast UI adapter is now a leaf library compiled against
the renderer-owned UI contract, the Fast rendering interface, and ImGui. It
does not link `three_ds_recomp_runtime`; the final implementation symbols continue to
arrive through the one remaining legacy application-host edge. This lowers
the reachable `three_ds_recomp_runtime` edge count from two to one without moving a Fast
adapter into renderer core or weakening any source-dependency ratchet.

The repeated Items-page run rendered the same 186 primitives with zero native
texture failures. Its complete framebuffer differs from the preceding run by
RMSE 0.00219482, confined to the independently advancing scene under the UI.

Evidence:

- `I:\oot3dre_work\native_game\renderer_dependency_audit_ui_leaf_20260724.json`
- `I:\oot3dre_work\native_game\renderer_ui_leaf_20260724.json`
- `I:\oot3dre_work\native_game\renderer_ui_leaf_20260724.bmp`

The native game entry point no longer includes or invokes `Ship::Context`.
Context teardown is paired with initialization behind the existing demo-host
boundary, reducing native frontend Ship-dependent files from 110 to 109. The
one remaining CMake edge is unchanged: context creation, window/input/audio
services and the non-A32 standalone fallback still reside in the legacy host
and must be split by responsibility before that edge can be removed.

The post-change Vulkan Items-page run exits cleanly after 100 presentation
frames, draws 186 host primitives, resolves its OoT3D texture with zero
failures, and reads TopScreen settings from external JSON. Complete-frame RMSE
against the immediately preceding run is 0.00149382.

Evidence:

- `I:\oot3dre_work\native_game\renderer_dependency_audit_entrypoint_context_20260724.json`
- `I:\oot3dre_work\native_game\renderer_entrypoint_context_20260724.json`
- `I:\oot3dre_work\native_game\renderer_entrypoint_context_20260724.bmp`

The obsolete `--n64-ui-archive` launch input has also been removed from the
native game contract and PowerShell launcher. Since the N64 archive texture
fallback no longer exists, accepting and mounting such an archive could only
hide a regression in the native texture path. The repeated Items-page run
reads 65,536 bytes through the OoT3D native texture provider, draws 186
primitives and records zero texture failures while using external TopScreen
JSON settings.

Evidence:

- `I:\oot3dre_work\native_game\renderer_no_n64_archive_20260724.json`
- `I:\oot3dre_work\native_game\renderer_no_n64_archive_20260724.bmp`

## Native application host split

`oot3d_native_game` is now exclusively the A32/OoT3D application. The former
non-A32 standalone scene path remains buildable as
`oot3d_native_game_legacy_sandbox`; it retains the old player controller and
cutscene/demo host without making either part of the product closure.

The product links `oot3d_native_application_host`, a four-source boundary
containing only context initialization/teardown, JSON I/O, framebuffer capture
and frame timing. The CMake closure falls from 25 to 24 targets, and neither
`oot3d_native_cutscene_host` nor its `oot3d_native_runtime_host` alias is
reachable. Both names are now forbidden by the dependency policy, so a future
accidental relink fails the audit even if the total ThreeDsRecomp edge count
does not increase.

Both `oot3d_native_game` and `oot3d_native_game_legacy_sandbox` build
successfully. A 100-frame Vulkan run from `hudtest_after120.oot3dsav` on the
new application closure draws 186 TopScreen primitives, reads 65,536 native
texture bytes, reports zero texture failures and measures 1.0912 seconds of
guest execution. Complete-frame RMSE against the preceding native run is
0.00316861.

The one remaining ThreeDsRecomp edge is now localized to
`oot3d_native_application_host`. Removing it requires renderer-owned
window/input/audio/context services; no gameplay, cutscene or sandbox source
needs to move with that work.

Evidence:

- `I:\oot3dre_work\native_game\renderer_dependency_audit_application_host_split_20260724.json`
- `I:\oot3dre_work\native_game\renderer_application_host_split_20260724.json`
- `I:\oot3dre_work\native_game\renderer_application_host_split_20260724.bmp`

## Commit discipline

Each significant step uses two commits when both repositories change:

1. a focused renderer integration commit;
2. a parent commit updating the submodule pointer, ratchet and documentation.

Commit messages and this document must state the removed edge, replacement
contract, tests run and any intentionally retained compatibility adapter.
