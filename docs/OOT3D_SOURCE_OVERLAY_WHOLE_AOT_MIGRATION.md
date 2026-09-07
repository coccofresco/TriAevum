# OOT3D source overlay over the qualified whole-AOT runtime

> **Historical snapshot.** The statement below that the qualified prebuilt
> shard archive remained valid applied to that source-overlay change only. The
> current branch has since changed the generated/runtime ABI and shard layout;
> do not reuse that archive. Rebuild it and follow the current contract in
> [OOT3D cross-platform performance status](OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md).

## Purpose

Use the known-working whole-AOT executable as the boot and rendering oracle,
then replace complete decompiled owners through a versioned DLL. A missing or
rejected source entry falls back to whole-AOT. The standalone source runtime is
not in this execution path.

This keeps the expensive runtime stable while source-owner iterations rebuild
only a small DLL. The final state remains a native source runtime; whole-AOT is
the temporary fallback and differential oracle while ownership is migrated.

## Build topology

The stable host is:

`F:\oot3dre_build\native-renderer-llvm\oot3d_native_game_source_overlay.exe`

The hot-reloadable module is:

`I:\oot3dre_work\native_game\source_overlays\oot3d_source_overlay.dll`

Normal owner iteration:

```powershell
.\scripts\oot3d\Build-Oot3dSourceOverlay.ps1 -RunTests
```

Measured on this machine after initial configuration:

- no-op owner invocation: about 1.3 seconds;
- one changed DLL translation unit: 2.3-4.0 seconds compile and link;
- changed DLL plus an already-built loader test: about 3.2 seconds total;
- initial CMake/Ninja configuration: about 17 seconds, paid once;
- initial standalone LLVM lowering-tool build: about 37 seconds, paid once.

Do not invoke the root build for an owner-only change. The build directory
retains the Visual Studio environment and Ninja dependency graph at
`I:\oot3dre_work\source-overlay-fast`.

Only an ABI or host-dispatch change requires:

```powershell
.\scripts\oot3d\Build-Oot3dSourceOverlayHost.ps1
```

That command compiles four bridge objects, including the small generated
whole-AOT registry, and links a separate executable against the
already-qualified objects and whole-AOT archive. Bridge compilation measured
10.8 seconds after the registry change and 0.4 seconds as a no-op; ThinLTO
relinks have measured 38-93 seconds depending on cache state and must remain
exceptional. Compile without linking while iterating on the boundary:

```powershell
.\scripts\oot3d\Build-Oot3dSourceOverlayHost.ps1 -CompileOnly
```

Its independent CMake tree is
`I:\oot3dre_work\source-overlay-host-patch`; it cannot alter the hot DLL cache.

## Migration inventory

The machine-readable inventory is:

`tools/oot3d/source_overlay/hybrid_migration_manifest.json`

It records dispatch precedence, the thirteen active hot-overlay entries, and
three distinct source inventories: eight accepted source gameplay owners, two
accepted CSAB leaves, nine reviewed typed entries, and the source-native work
that is preserved while acquiring mixed-A32 adapters. The validator computes
overlap with the active DLL, so an accepted owner is never silently counted as
active merely because its source package exists. Validate the inventory with:

```powershell
python .\tools\oot3d\source_overlay\validate_hybrid_migration_manifest.py
```

The source-native startup closure and its 18 address-owned boot contracts were
preserved at checkpoint `243cc5632`. Three contracts are now promoted through
isolated mixed-boundary adapters: `PauseItemsPage_InitializeState` at
`0x00435C54`, the maintained target `memset` at `0x00303B14`, and the
identity memory-stream destructor at `0x003FFB7C`. The other 15 remain pending
rather than discarded or silently activated.

## Run loop

Interactive Vulkan run from the qualified savestate:

```powershell
.\scripts\oot3d\Invoke-Oot3dSourceOverlayGame.ps1
```

This default migration lane enables the thirteen entries in the hot source-overlay
DLL. The qualified executable predates the later gameplay-profile switches, so
typed gameplay, manual compiled functions, mass-AOT and true-AOT are disabled;
every remaining entry falls through to the qualified whole-AOT. Accepted owner
packages remain available for measured import into the hot DLL without a broad
host relink.

Skip even the fast DLL build when only rerunning:

```powershell
.\scripts\oot3d\Invoke-Oot3dSourceOverlayGame.ps1 -SkipOverlayBuild
```

The launcher keeps the proven whole-AOT baseline as the final fallback and sets
`OOT3D_SOURCE_OVERLAY` only for the child process. Vulkan/NRI remains the target
renderer.

Latest bounded verification after promoting the three leaf contracts:

```powershell
.\scripts\oot3d\Invoke-Oot3dSourceOverlayGame.ps1 `
    -SkipOverlayBuild -LoadState '' -Frames 6 -MaxSeconds 30 -DisableAudio
```

The initial boot completed within the bounded run and reported 13 loaded
entries, `pause_state=1/1`, `boot_leaves=1/1`, and `rejected=0`. It handled
723/723 overlay calls with zero ABI mismatches. The qualified quick-state route
handled 479/479 calls with zero ABI mismatches.

## ABI and fallback

The public C ABI is in
`tools/oot3d/source_overlay/oot3d_source_overlay_abi.h`. It exposes:

- registered guest entry points with owner and symbol identity;
- the complete A32 architectural state at a source/AOT boundary;
- checked guest-memory read, write, resolve and probe callbacks;
- an opt-in decoded-A32 differential oracle for source-owner validation;
- branch, SVC, block-limit and memory-fault results.

The host loads the DLL from `OOT3D_SOURCE_OVERLAY`. Every registered entry is
also inserted into the whole-AOT observable-boundary set, so direct calls from
generated AOT code return to the dispatcher. Recursive calls to the same active
entry bypass the overlay and use the fallback.

The two pure boot leaves are isolated in
`tools/oot3d/source_overlay/oot3d_source_overlay_boot_leaves.cpp`. The target
`memset` uses the checked guest-memory resolver and preserves the maintained
destination/size/value contract; invalid mappings return `NOT_HANDLED` without
mutating guest state. The identity destructor has no guest-memory or external
service dependency and only returns through the target link register. Neither
adapter imports the whole-AOT implementation.

The host `CallGuest` diagnostic callback deliberately bypasses the source
overlay, manual C++ replacements and whole-AOT externals. It executes the
generated decoded-A32 registry one basic block at a time until the original
`lr`, so validation remains available even for helpers that whole-AOT had
externalized. This path is disabled during normal play and is not a runtime
dependency of a promoted owner.

Generated functions promote A32 registers into host locals. Throwing out of a
nested generated call previously let outer scope destructors overwrite the
exact callee boundary with stale caller locals. Observable exits now capture
the complete `GuestState` in ABI-compatible thread-local storage; the central
registry restores it after the final promoted-state flush. The exception layout
is unchanged, so the qualified prebuilt shard archive remains valid. The
authoritative change is emitted by `whole_aot_cpp.py`; the current external
registry under `whole_aot_optimization\scalar_full` follows the same contract.

An overlay entry must return `not handled` before mutating state when its
preconditions are not met. This preserves safe fallback.

## Boundary correctness

Source-level meaning alone is insufficient while a source owner still has AOT
callers. ARM caller-clobbered registers, `NZCV`, guest stack spills and return
shape can remain observable at that mixed boundary.

The first migrated entry, `oot3d_memcpy_aligned_end@0x00371738`, demonstrated
the rule. A plain bulk copy rendered correctly for a few frames but diverged by
seven guest bytes. The `code.bin` instructions showed the missing stack spill
and final `r2/r3/r12/NZCV` effects. The final implementation uses a bulk source
copy plus a compact exact ABI adapter. It handles 394 calls in the bounded test
and produces a byte-identical savestate to whole-AOT; only the host-internal
memory write-generation counter differs because one bulk write replaces many
ARM stores.

Compare two boundary savestates with:

```powershell
python .\tools\oot3d\source_overlay\compare_savestates.py `
  baseline.oot3dsav candidate.oot3dsav
```

The comparator validates the container shape and requires equality of the full
state after ignoring only `memory.write_generation`. Add
`--verify-container-checksum` for the slower full payload checksum audit; normal
iteration relies on files emitted atomically by the runtime.

## Owner migration rules

1. Migrate a dependency-closed owner, not isolated high-level functions.
2. Register every source entry that can be reached directly from AOT.
3. Keep guest addresses as 32-bit values; never store host pointers in guest
   structures.
4. Lower guest dereferences through the host memory API until a direct-mapped
   guest-memory backend is deliberately introduced.
5. Generate boundary adapters from original entry/exit semantics. Do not infer
   them from C prototypes alone.
6. Validate a small deterministic boundary first, then a longer interactive
   boot/cutscene window.
7. Remove an AOT fallback only after all entries in that owner closure pass.

## Decompiled owner pipeline

`runtime.memory` is the first complete copied owner. Its source snapshot and
provenance are under:

`tools/oot3d/source_overlay/decomp_owner/runtime_memory`

The build compiles the C source to LLVM bitcode, runs `mem2reg`, applies the
standalone `Oot3dGuestMemoryLowering` tool, and emits one optimized COFF object
for the overlay DLL. The lowering tracks guest-pointer provenance through
casts, GEPs, PHIs, selects and pointer/integer arithmetic. It rewrites scalar
and aggregate loads/stores, pointer serialization, and memory intrinsics to the
checked host callbacks. `--strict` rejects residual raw guest dereferences.

Build the lowering tool independently with:

```powershell
.\scripts\oot3d\Build-Oot3dSourceOverlayLlvmPass.ps1
```

The loader test covers the six original runtime-memory entries, the game-state
and cutscene owners, the pause-state contract, and the two boot leaves. The bounded
six-frame Vulkan validation produced:

| entry | handled calls | VBlank | draws | content fingerprint |
| --- | ---: | ---: | ---: | --- |
| `Lib_MemSet` | 0 in this window; unit-tested | 6 | 472 | `14731610133655071309` |
| `Oot3d_ClearTenBytes` | 2 | 6 | 472 | `14731610133655071309` |
| `oot3d_memclear_counted_8byte_entries` | 3 | 6 | 472 | `14731610133655071309` |
| `oot3d_memclear` | 9 | 6 | 472 | `14731610133655071309` |
| `oot3d_memcpy_end` | 224 | 6 | 472 | `14731610133655071309` |
| `oot3d_memcpy_aligned_end` | 394 | 6 | 472 | `14731610133655071309` |

All six enabled together now handle 414/414 observed owner calls in the bounded
window. The reduction from the earlier 632 boundary count is intentional:
`oot3d_memcpy_end` completes its source-level tail call inside the owner instead
of exposing 218 internal helper transitions to the dispatcher. With
`OOT3D_SOURCE_OVERLAY_VALIDATE_ABI=1`, every observed entry is compared against
the decoded-A32 registry and the run reports zero ABI mismatches. With
validation disabled, no owner entry calls `CallGuest`.

The independent six-frame Vulkan run matches the qualified baseline exactly:
6 VBlanks, 472 draws and memory-content fingerprint
`14731610133655071309`, with zero whole-AOT memory faults or unsupported exits.

## Game-state owner checkpoint

`GameState_Update@0x00417014` and its callback continuation at `0x00417024`
are now served by the copied, accepted source owner. The adapter preserves the
validated ARM32 callback frame while `GameState.main` may still resolve to a
source or whole-AOT entry; the data operations themselves are compiled from C
and lowered through the checked guest-memory ABI.

The six-frame Vulkan validation observed three owner entries and three callback
returns. All six were handled by the source overlay, with no ABI-oracle call,
and produced the same 6 VBlanks, 472 draws and fingerprint
`14731610133655071309` as the qualified runtime. The loader regression also
checks the saved callback frame, continuation, frame-counter update and final
register state.

## Next implementation block

Do not reconstruct another owner contract. Commit `c461a1cfd` imports the
historical accepted differential implementations for Actor_UpdateAll,
Actor_InitContext, Cutscene_UpdateFrame, Cutscene_ProcessCommands,
Camera_Update, Player_Update, Player_UpdateCommon and their composed
source-gameplay profile. Their original per-owner switches, tests and
provenance manifests remain intact.

`Cutscene_UpdateFrame@0x00321F50` is the first of those accepted owners now
executed through the hot overlay. The DLL compiles the unchanged imported
source body from:

`tools/oot3d/source_imports/cutscene_update_frame_d5c2927`

Its adapter reuses the accepted literal-cell, `SP-0x18`, hard-float and FPSCR
contracts. A deterministic 60-frame Link-dream comparison produced identical
guest frame 2999, system tick `13401124269`, guest-memory content fingerprint
`1368393024711753443`, 5,181 draws, 118 display transfers, 119 VBlanks,
framebuffer state and DSP PCM. The source owner handled 59/59 calls with zero
failures.

`Cutscene_ProcessCommands@0x002C5BA0` is now compiled from the unchanged,
accepted `cutscene_process_commands_d5c2927` import and composed directly with
the frame owner inside the overlay. The adapter preserves the accepted 69
literal cells, checked guest pointers, `SP-0x100` dependency frame, scratch
copy-in/copy-out, hard-float arguments, conversions, dynamic callbacks and
FPSCR propagation. Its residual target-address dependencies remain explicit
and separately counted; they are not mislabeled as source closure.

The deterministic 60-frame Link-dream composition test again reached guest
frame 2999 and tick `13401124269`, with content fingerprint
`1368393024711753443`, 5,181 draws, 118 display transfers, 119 VBlanks, 329
PICA completions, identical framebuffer state and identical DSP PCM fingerprint
`12827258994556491197`. The frame owner made 59 direct source calls to the
parser and zero A32 calls. The parser handled all 59 calls with zero failures;
its 603 still-unpromoted dependency calls are now the measured residual set.
Only the expected memory-state and process-state implementation fingerprints
differ from the decoded-A32 baseline.

Continue by promoting the parser's measured residual dependency set in coherent
owner groups, then import the remaining accepted gameplay owners in measured
batches. Use live residual accounting plus the captured 47-function startup
slice to select boot work. The standalone source-native process-image,
CTR-service and pointer32 work remains the target composition root; the
whole-AOT host is only the temporary executable oracle during migration.
