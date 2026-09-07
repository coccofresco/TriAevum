# OOT3D source-native runtime

## Decision

The production replacement is a new source-native composition root in this
repository. It reuses the validated renderer, native asset, platform, audio,
input, UI and diagnostic modules, but it does not execute gameplay through
the A32 interpreter, whole-AOT archive or per-owner typed-or-A32 bridges.

`oot3d_native_game` remains unchanged as the behavioral oracle while the new
path reaches parity. Room Compilation Unit and manual actor runtimes remain
diagnostic tools; canonical OOT3D source owns production gameplay.

## Pinned producer input

The current producer checkpoint is `I:\oot3decomp` revision
`ded8104e66eabe336b389e6a6a3510cb36ac8b58`. The consumer profile is
`tools/oot3d/source_native_runtime/decomp_profile.json`.

The source tree is not copied into Git. Run:

```powershell
scripts\oot3d\Prepare-Oot3dSourceNative.ps1 -ReplaceSnapshot
```

This uses `git archive` against the pinned commit, extracts only the 1,065
canonical `portable_semantic` C sources plus their headers and metadata, hashes
every source and writes `source_snapshot_manifest.json` below the ignored build
directory. Local uncommitted producer changes cannot enter the package.

## Runtime boundaries

- `GuestAddressSpace` owns target-addressable code/data/heap mappings without
  exposing ARM registers or inheriting from an A32 memory bus.
- `SourceFunctionRegistry` maps target callback addresses to signature-checked
  host functions.
- The future process-image layer loads `code.bin` data into the guest address
  space. Preserving the title's 32-bit address model is not A32 execution.
- Original source owns actor, player, camera, cutscene, material and audio
  behavior. Host modules provide OS, presentation, input and DSP services.
- PICA command submission continues through the existing backend-neutral
  frontend and NRI renderer.

## Acceptance sequence

1. Compile the complete pinned source surface on the host.
2. Resolve target globals, pointer32 values and indirect functions through the
   two architecture-neutral contracts.
3. Link `nnMain` with a finite, explicit platform-import whitelist.
4. Reach first VBlank with matching PICA and DSP event fingerprints.
5. Validate boot, file select, Link's dream, Navi flight and Kokiri gameplay.
6. Make the source-native executable default only after those checkpoints run
   with zero A32 calls.

The first bootstrap target is `oot3d_source_game`. It intentionally links no
A32 runtime and can verify that a materialized snapshot matches the compiled
consumer profile.

The module has its own CMake entry point. `Prepare-Oot3dSourceNative.ps1
-Build` configures that entry point directly, so source-runtime iterations do
not install or scan the legacy Ship dependency graph.

Pass `-BuildSurface` to compile all 1,062 canonical C sources into
`oot3d_source_surface_archive`. This is a compile gate, not an executable and
not an A32 fallback. Its unresolved-symbol inventory defines the finite host
data, callback and platform contracts that the next migration stages must
implement.

The surface gate uses the pinned LLVM 22.1.6 toolchain. GCC 16.1 reaches most
of the surface but crashes internally in its RTL final pass on
`z_whole_residual_21_all_residual_21_25.c` at `-O2`; Clang compiles the same
unchanged unit successfully. `analyze_source_surface.py` subtracts all symbols
defined elsewhere in the archive and writes `source_surface_contracts.json`,
which is the authoritative host-contract backlog.

## df60d5b consumer checkpoint

The first complete consumer build produced these results:

- 914/914 canonical sources compiled and archived by Clang 22.1.6;
- 205 seconds for the initial parallel build and about 1.1 seconds for a no-op
  incremental build;
- 35,000 unique symbols remain external after subtracting every definition in
  the complete archive;
- 32,609 are target data addresses, 76 are target function addresses, 1,489
  are source globals and 816 are other source/platform functions;
- the bootstrap and contract tests pass, and the bootstrap contains no A32 or
  whole-AOT symbol.

The dominant next task is therefore target data-image binding, followed by
function-address dispatch and the explicit platform import set. Per-owner A32
replacement is no longer the migration unit.

## Direct process-image and data binding checkpoint

The runtime can now mount the complete static process image from the pinned
CTR manifest: verified `code.bin` segments, system regions, heap, linear heap,
primary-thread TLS, and the primary stack range contained in the heap
reservation. `SourcePrimaryThreadDescriptor` retains the initial thread state;
the loader rejects profiles whose stack/TLS mappings or thread pointer are
invalid. On Windows the direct mapper coalesces contiguous reservations and
changes protection only at actual reservation boundaries. Demand-zero regions
are mapped directly, without temporary 128 MiB/64 MiB vectors. A source pilot
binds `DAT_00314094` as an absolute
linker symbol and reads/writes the real mapped data image successfully. This
preserves direct pointer casts used by the canonical source without introducing
an A32 memory bus or a per-access lookup.

## Host import contract

`host_import_contract.json` is the finite allowlist for ordinary imports in the
legalized source archive. The pinned surface currently requires seven standard
C runtime functions, one compiler stack-probe helper, three CTR imports and 15
typed host hooks.
`validate_host_import_contract.py` runs after surface analysis and rejects new,
removed or reclassified imports.

The two CTR calls are implemented by `oot3d_ctr_host_services`, which binds a
typed service provider and the primary thread's real TLS command buffer. Both
the producer's host-resolver entry points and the original `svc*` ABI names use
the same bridge. Missing service bindings fail immediately; the runtime never
returns fabricated IPC success.

`oot3d_ctr_ipc_router` owns the architecture-neutral handle-to-session map and
records every dispatch/close result. It deliberately contains no ARM register
state and no service-specific command constants. Concrete services implement
`CtrIpcSession`; the first migrated service is `cfg:u`, whose language, region,
sound mode and stereo-camera values come from an injected
`CtrConfigServiceProfile`. Its IPC descriptor validation and guest-memory
writes match the previously validated CTR host implementation.

Session creation is not guessed in the consumer. The producer exposes
the inline `ConnectToPort` boundary as a host hook; the source-native router now
assigns a handle only for explicitly registered ports.

## ded8104e consumer checkpoint

The current producer surface passes the complete consumer pipeline:

- 1,065/1,065 canonical units compile and pass strict pointer-storage legalization;
- 115 absolute target-data bindings are generated;
- the complete external closure contains 43 symbols: six non-reachable
  source/platform functions and 37 verified compiler, C runtime, CTR and typed
  host-hook imports;
- the `nnMain` reachable closure contains zero unresolved identities and zero
  multiple definitions;
- the producer supplies zero external semantic globals and zero external target
  function addresses through its native data and callback registries;
- all 37 host imports match the explicit allowlist.

The exact owner package `ded8104e66ea-6e7b2907b523` now classifies all eight
configured owner graphs as source-closed, with 1,216/1,216 unique closure
entries and no semantic worklist. Historical semantic-certification rows are
ignored after the same entry is promoted into reviewed/typed source; they no
longer require a retired bulk source file to remain in the active lane.

The profile's 9,274 target candidates must not be confused with the 9,184
dispatchable callbacks sealed by the native registry. The producer separately
classifies MobiClip internal control-flow entries. Its legacy aggregate
`reviewed_target_linked_c_bodies` denominator is currently inconsistent
(9,274/9,242), so owner closure and executable-closure audits are the
authoritative acceptance metrics until that producer report is revised.

The title-owned four-argument `mbstowcs` still collides by name with the host
CRT and therefore retains its isolated ABI rename. Address-derived linker
aliases are no longer generated or consumed: the producer registry closes the
complete target-function surface directly.

## Historical ed3823a consumer checkpoint

The updated producer surface passes the complete consumer pipeline:

- 917/917 canonical units compile and pass strict pointer-storage legalization;
- external closure reduced from 2,144 to 1,902 symbols;
- semantic globals reduced from 1,489 to 1,389;
- source/platform functions reduced from 554 to 432 after classifying the TLS
  command-buffer import;
- target-address functions reduced from 76 to 69;
- all 15 invalid/constant-like target-data identities are closed;
- `ConnectToPort` is enabled and routed through the source-native IPC registry;
- the explicit host allowlist contains 12 verified imports.

Without further decompilation, work can proceed on concrete CTR sessions (SRV,
HID, GSP, DSP and FS), filesystem/audio/input adapters, renderer submission,
bootstrap, diagnostics and savestates. The remaining globals, source functions
and target callbacks remain producer-owned and must not be reconstructed here.

The first of those verticals is now active. `SourceCtrRuntime` composes the
architecture-neutral router with native `srv:`, `cfg:u` and `fs:USER`
sessions. The
producer's `ConnectToPort("srv:")` hook receives a runtime-assigned handle;
SRV `RegisterClient` and `GetServiceHandle` then open registered services using
the original CTR IPC command/response layout. Notification semaphores remain
intentionally unimplemented until the host kernel wait/synchronization module
is available; no placeholder semaphore is returned.

The first read-only filesystem vertical is also complete. `fs:USER` accepts
the native Initialize and SelfNCCH OpenFileDirectly commands, creates a dynamic
file session, and implements native GetSize, Read and Close replies. Reads go
directly from a profile-supplied RomFS image range into validated guest memory;
there is no extracted-asset substitution and no path embedded in the runtime.
The launcher accepts `--romfs-image`, `--romfs-offset` and `--romfs-size`, with
the size inferred from the image when omitted. A composed regression test
crosses `srv:` -> `fs:USER` -> file handle and verifies offset-correct bytes in
the guest address space.

The persistent SaveData vertical is now present as well. Archive ID 4 mounts a
profile-supplied directory and retains the original binary/UTF-16 low-path,
archive-handle and IPC descriptor contracts. Confined files support Open,
Create, Read, Write, GetSize, SetSize, Flush and Close; ControlArchive and stale
archive handles preserve their observed CTR results. `--savedata <directory>`
selects storage at launch. Tests exercise persistence and reject paths escaping
the archive root. Delete/Rename and directory sessions remain deliberately
unimplemented until they appear on the source boot/gameplay path.

The handle layer no longer assumes that every CTR handle is an IPC session.
`CtrIpcRouter` owns typed kernel objects independently, allocates from one
collision-free handle namespace, duplicates shared object identity, and closes
individual references. The first consumer is native `APT:U`: GetLockHandle and
Initialize return a shared mutex and notification/parameter events with the
observed CTR descriptors; the initial Wakeup event starts signaled. Enable and
NotifyToWait preserve their native replies. Waiting on these objects is not
fabricated: execution still requires the producer's typed wait-SVC boundaries
documented in the decomp handoff.

Native `hid:USER` now owns the original six-handle contract: one 0x1000-byte
shared-memory object and five pad/touch/motion events. Sensor enable/disable is
reference-counted and the native gyroscope coefficient and calibration payloads
are profile data. `CtrHidProducer` writes the original pad and touch ring-buffer
layout, including reset ticks, current state, press/release edges, circle-pad
direction bits and event signaling. It writes host-neutral HID state rather than
window-system input, so keyboard, mouse and controller adapters remain outside
the CTR ABI module. The source runtime exposes the HID endpoint directly.
Mapping the same shared object into guest virtual memory and consuming its
events still require typed MapMemoryBlock and WaitSynchronization producer
hooks; no duplicate buffer or polling fallback is used.

The consumer-side kernel contract now implements the corresponding mechanics:
shared memory can be mapped onto an already reserved guest process-image range,
after which producers write that mapping directly rather than copying a shadow
buffer each sample. Immediate wait-any/wait-all preserves selected indices,
auto-reset events, semaphore counts and mutex ownership; event signal/clear and
mutex release operate on shared object identity across duplicated handles.
These methods are architecture-neutral and tested through the HID mapping. The
remaining blocker is only ABI reachability from canonical source: generic
`software_interrupt(number)` sites still do not carry the argument registers
needed to call these methods safely.

APT startup now also consumes the primary thread's real static-buffer table.
ReceiveParameter emits the initial Wakeup payload and AppletUtility handles the
observed sleep-shell/unlock-transition commands without an A32 TLS model.
`SourceCtrRuntimeProfile` groups all injected service profiles and derives this
address from the mounted process thread descriptor.

Native `gsp::Gpu` is split into a CTR decoder and `CtrGpuBackend`. The decoder
implements cache coherency requests, AcquireRight, interrupt-relay shared
memory, LCD state, framebuffer swaps, register writes and the native 15-entry
command queue. SubmitCommandList payloads are validated and read directly from
guest memory before reaching the backend. Tests execute a real queue transition
and preserve its header state. The next consumer task is an adapter to the
existing PICA frontend/NRI sink plus interrupt-ring delivery; the decoder does
not contain renderer policy.

That adapter is now implemented by `CtrPicaBackend`. Native GSP register writes,
masked writes, command lists, framebuffer swaps and LCD state reach the existing
backend-neutral PICA frontend. PICA interrupts are returned through the native
GSP relay ring and signal its event. This closes command decoding without
embedding renderer or window policy in the CTR service.

Native `dsp::DSP` now implements the observed component load, interrupt-event,
semaphore, process-pipe, address conversion and audio-state startup contracts.
Its 15-entry shared audio structure table is mapped in guest memory and DSP
interrupts use typed kernel events. `ndm:u` scheduler suspend/resume is also
available. Actual host audio mixing and presentation remain downstream adapters;
their CTR IPC boundaries no longer require reconstruction.

The validated Azahar HLE mixer now also accepts an architecture-neutral memory
contract. `SourceDspMixer` reads the two original DSP shared-memory regions,
resolves sample buffers through the process descriptor's physical mappings,
writes native DSP status back to guest memory and emits one 160-sample stereo
frame per native audio tick. The former `NativeA32Memory` path is retained in a
separate adapter. PICA and DSP third-party subsets are distinct static libraries
so their incompatible Azahar `common/*` compatibility headers cannot leak
across backend boundaries. Feeding emitted PCM to the host audio device belongs
to the future source application loop, not to the CTR IPC service.

The PICA submission queue is no longer coupled to `NativeA32Memory`. Its core
accepts a checked read view and the legacy runtime supplies its former behavior
through a separate compatibility adapter. `SourcePicaSubmission` derives the
native 3DS physical mappings for linear memory and VRAM from the process-image
descriptor, reads geometry and textures directly from `GuestAddressSpace`, and
is installed as the source frontend's packet sink. No A32 memory bus, asset
conversion or per-draw process-image copy is involved. Presentation of these
captured submissions through the NRI window remains the next consumer adapter.

## `nnMain` closure gate

`oot3d_source_link_probe` links the real legalized source archive from `nnMain`
against the complete consumer runtime. Its expected linker failure is parsed by
`Invoke-Oot3dSourceLinkProbe.ps1` into
`build-source-native/link-probe/nnmain_link_contracts.json`. At the current
pinned producer revision, the reachable closure contains zero unresolved
symbols and zero multiple definitions. The archive rescan resolves every CTR
import and typed host hook implemented by the consumer. The generated source
uses the name
`mbstowcs` for a title-owned four-argument routine, which collides with the host
CRT despite having a different ABI. The lowering pass now lexically renames
only that title symbol and its call sites to `oot3d_target_mbstowcs`; it injects
one missing old-style declaration where the source previously inherited the
unrelated CRT prototype. The gate consequently reports zero duplicate symbols
without allowing arbitrary multiple definitions.

The producer-owned target-function registry now closes the complete indirect
surface. The consumer no longer derives linker aliases or applies `--defsym`
function identities.

The source closure is now link-complete. CTR services, process mapping and PICA
submission remain consumer-owned; launch work can proceed without guessed
stubs, generic `software_interrupt(number)` handling or A32 fallback.

The source application now starts an isolated `SourceHostPump` before entering
`nnMain`. It samples the native HID shared-memory rings at 60 Hz and advances
the DSP HLE at the title's exact 32,728 Hz / 160-sample cadence. Input and PCM
delivery are callback boundaries, so the runtime contains no window or audio
device policy. A launch against the original `code.bin`, RomFS image and
SaveData remains active past ten seconds after process mount, CTR startup,
host-pump startup and registration of the complete target-function surface.

Visible launch still requires one composition adapter in the root application:
consume `SourcePicaSubmission::Queue()` through the existing
`Oot3dPicaVulkanBridge`/NRI presentation path, feed `Fast3dWindow` input into
the host-pump input callback and route its PCM callback to the existing audio
device. This adapter must reuse `Ship::Context` and must not copy the A32 window
loop into the source runtime.
Run the gate after every pinned source update with:

```powershell
scripts\oot3d\Invoke-Oot3dSourceLinkProbe.ps1
```

Clang AST, rather than textual pattern matching, classifies every target-data
declaration. The generated lowered snapshot changes only external
`uintptr_t`/`intptr_t` declarations proven to represent 32-bit guest words;
real host-width pointers, aggregates and conflicting declarations remain
untouched. Symbols outside the process map are never exported as absolute
addresses.

At revision `df60d5b` this pass produces:

- 914/914 lowered translation units compiled;
- 620 source files touched in the ignored generated snapshot;
- 17,283 mapped guest-word symbols lowered across 17,896 declarations;
- 11,954 already width-safe mapped data symbols bound directly;
- unresolved symbols reduced from 35,000 to 5,763;
- unresolved target-data symbols reduced from 32,609 to 3,372;
- about 0.97 seconds for a no-op lowered archive build.

The remaining target-data set is intentionally not coerced: it consists mainly
of 2,887 pointer-width declarations, 243 declaration conflicts and a small set
of aggregate or missing declarations requiring semantic typing. Reproduce the
complete checkpoint with:

```powershell
scripts\oot3d\Prepare-Oot3dSourceNative.ps1 -ReplaceSnapshot -Build `
    -BuildLoweredSurface
```

The lowered archive is a host-source integration artifact, not generated
gameplay and not an A32 fallback. The canonical snapshot remains unchanged and
continues to compile as an independent regression gate.

## Pointer32 legalization

The remaining host-width pointers cannot be represented by eight-byte globals
over the packed four-byte 3DS data image. They also must not be converted to
plain integers in C, because that discards typed dereference, indexing and
callback semantics. `llvm_pass/Oot3dGuestStorageLegalizer.cpp` starts the
correct target-specific lowering at LLVM IR level:

```llvm
; canonical host IR
%pointer = load ptr, ptr @DAT_0016cb94, align 8

; legalized process-image access
%guest32 = load i32, ptr @DAT_0016cb94, align 4
%pointer = inttoptr i32 %guest32 to ptr
```

The standalone transformer is used because this Windows LLVM distribution is
static and its `opt` executable cannot load pass plugins. It is deliberately
kept as a separate offline executable. Before IR generation, the generated
snapshot uses a C lexer to lower decomp-owned `uintptr_t`/`intptr_t` tokens to
the explicit 32-bit guest ABI; comments and literals are never rewritten. This
makes pointees, parameters and pointer arithmetic target-correct. The LLVM pass
then legalizes only global pointer storage and propagates that provenance
through casts, `phi` and `select` nodes. Strict mode rejects every escape it
cannot prove.

The complete strict pass at `df60d5b` produces:

- 88,492 guest-ABI identifier replacements in 810 generated files;
- 914/914 translation units legalized with zero unsupported storage uses;
- 32,124 mapped data symbols bound to the process image;
- unresolved symbols reduced from 5,763 to 2,876;
- unresolved target-data symbols reduced from 3,372 to 485, including the 15
  known unmapped identities;
- 2.66 seconds for a fully cached 914-unit no-op rebuild.

Build and audit this archive with:

```powershell
scripts\oot3d\Prepare-Oot3dSourceNative.ps1 -ReplaceSnapshot -Build `
    -BuildLlvmLegalizedSurface
```

All 32,856 symbols inside the process map are now absolute-bound. This includes
Ghidra `PTR_*`, string and UTF data labels that were previously misclassified
as functions. Aggregate and
conflicting declarations remain visible in the generated audit, but they do
not block linkage: the symbol address is invariant, scalar accesses retain
their source type and every pointer-storage access passes strict IR
legalization. Only 15 constant-like or invalid identities outside the process
map remain unresolved. The next largest closures are 1,489 source globals, 554
source/platform functions and 76 target-address functions; their producer
requirements are tracked in `OOT3D_SOURCE_NATIVE_DECOMP_HANDOFF.md`.
