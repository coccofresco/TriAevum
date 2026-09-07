# Whole-AOT to local game module migration

## Finding

The validated whole-AOT product is reusable as a code-generation oracle, but
its current static archive is not independently playable. It contains 12,419
generated functions and their dispatcher while `oot3d_native_game` still owns
the process scheduler, guest memory, SVC routing, title bootstrap and all host
adapters.

The three host boundaries recorded in
`whole_aot_product_manifest.json` are atomic functions intentionally excluded
from generated code:

- `PicaCommandWriter_WriteRegisterRange`;
- `Mtx3x4_Multiply`;
- `Mtx3x4_CopyIfDistinct`.

They are not the complete runtime boundary. Treating them as such would produce
a module that links but cannot boot.

## Required ownership split

The private OoT3D module owns:

- generated title functions and dispatcher;
- guest CPU/process state and 32-bit address-space memory;
- title image initialization and entrypoint execution;
- title-side scheduler state and portable serialization;
- SVC requests up to, but not including, host implementation.

The public TriAevum runtime owns:

- window, timing and presentation;
- NRI/PICA renderer services and pipeline caches;
- audio device/mixer output;
- filesystem sessions over the private `content.tap` provider;
- abstract input/HID, configuration and save-state storage;
- diagnostics and lifecycle orchestration.

TopScreen and other title policy remains in a separately reviewable title
adapter. Original mod payloads and title assets remain private Forge inputs.

## Fast migration sequence

1. Extract a title-process core library from the existing runtime without SDL,
   NRI, audio-device or window dependencies. Its only outward calls use typed
   module service IDs.
2. Wrap the existing cached whole-AOT archive and process core in
   `oot3d_game_module`, exporting `TriAevumQueryModuleV1`.
3. Route PICA, audio, FS, HID, time and thread operations through host-service
   adapters that call the existing implementations. Do not rewrite those
   implementations during the split.
4. Add a developer mode in the current product host that loads this module via
   `LoadedNativeModule`; preserve the linked executable as the oracle.
5. Run the existing golden framebuffer/PCM/resume matrices linked-vs-module.
   Require exact output and strict counters before changing the default.
6. Add a Forge link-only step that reuses the content-addressed whole-AOT
   archive and emits the private DLL/TAM. Runtime edits then never rebuild title
   code.
7. Replace generated C++ shards with direct structural-IR-to-object emission.
   The module ABI and validation suite remain unchanged.

The shared vocabulary required by steps 1-3 is implemented in
`runtime/triaevum_module/include/triaevum/service_abi.h`. It deliberately
describes observable host operations, not current runtime classes. Adapters may
therefore be introduced around the existing PICA/NRI, audio, filesystem and
input code without copying those systems into the private title module.

The first such adapter now reaches the existing OoT3D PICA frontend through
typed register/GSP operations, validated memory leases and an ordered NRI batch
consumer. Renderer fence completions return through the typed module interrupt
queue. Generic CTR scanout state now selects presentation from native
framebuffer requests. The inverse module-side client and the CTR process bridge
now route all native GPU operations through that ABI while preserving the
direct linked oracle. Forge now derives and verifies the real process manifest,
including entrypoint, linear heap and CTR VRAM mapping, from the user's own
ExHeader and content.

The module lifecycle is now implemented by `oot3d_game_module`: it mounts the
verified image, advances native time/VBlank/DSP events, routes PICA, PCM,
filesystem and abstract input through public service clients, exposes bounded
guest-memory leases and serializes process, CTR-host and PICA sequencing state.
It imports no renderer, SDL, audio-device or host-filesystem implementation.
The independent smoke host loads the Forge-produced TAM and has passed the real
entrypoint plus 120 frames; the graphical host has also presented the resulting
native scene through NRI/Vulkan. Direct local object generation and broad
linked-versus-module equivalence are now the next boundaries; see
`TRIAEVUM_GAME_MODULE_VERTICAL.md`.

The link-only bridge makes architecture and fidelity testable quickly. It does
not solve end-user setup by itself: a public Forge cannot distribute the cached
archive, so direct local object generation remains a release gate.

## First vertical acceptance test

The first OoT3D module need only reach the original process entrypoint and one
observable host request. It must prove:

- no title code is linked into the test runtime executable;
- the module's source hash matches the supported recipe;
- guest memory and state live entirely behind the module instance;
- one real SVC/PICA request crosses a typed host service;
- linked and module execution produce identical state at that boundary;
- rebuilding the runtime does not rebuild or relink the module.

The lifecycle, identity, memory-ownership and observable PICA-request portions
are proven. Exact linked-versus-module state comparison remains open. Extend
from that comparison by service surface and the existing scenario matrix rather
than by scene-specific game behavior.
