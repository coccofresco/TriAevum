# OOT3D consumer-to-decomp handoff

## Purpose

This document is the stable entry point for semantic work reconstructed in the
native runtime before the same owner exists as reviewed source in
`I:\oot3decomp`. The decomp checkout remains a read-only producer input here.
No consumer script switches its branch, edits its files or commits into it.

The machine-readable transfer is generated below:

```text
I:\oot3dre_work\source_integration\<snapshot>\
  decomp_handoff.json
  decomp_handoff.md
  decomp_work_request.json
```

`I:\oot3dre_work\source_integration\latest.json` identifies the latest package.
The JSON handoff is authoritative for paths, hashes and structured evidence;
this document explains how to consume it.

## Transfer states

`consumer_reviewed_semantic` means that the consumer independently recovered a
target-linked semantic body, implemented it and tested it against the existing
A32 path. It can close a consumer owner graph, but it is not producer source.

`reviewed_semantic` means that the decomp has reviewed and published its own
target-linked source. On the next synchronization that producer status wins
automatically and the consumer overlay is reported as superseded.

Runtime activation is a separate decision. An active owner must have a hashed
`owner_import_acceptance` record covering:

- ABI and guest layout;
- Native30 differential behavior;
- callback and fallback behavior;
- a checkpoint round trip through captured guest state;
- Enhanced60 timing ownership;
- the precise A32 work removed.

## Current package

The current package is:

```text
I:\oot3dre_work\source_integration\6a1197f3a520-7286a857de90
```

It is pinned to producer commit
`6a1197f3a5208930e5acaedcf08fbf60656548a9`. The producer worktree may
advance independently; regenerate the package before import rather than
reading its uncommitted files.

Its callgraph SHA-256 is
`4e029ac39fbf52df6c93a45ec4f07f19d9a9810eeb6f208eaeae6376860c4f40`;
its function-pointer-reference SHA-256 is
`b8cb9471623dd04a8eeddb1b0f0b09096c0b8a227792fc612ed9627df9f1e737`.
The package manifest SHA-256 is
`9fcbd1726b2a8976363d12b08b3489b8b512b31709db10c3e5e5148d30b2de94`.
Committed producer files are read through Git and copied into the immutable
package; no tracked producer worktree edit was observed.

This checkpoint contains 3,361 producer-reviewed target-linked bodies, 1,125
semantic-certification records, nine superseded consumer overlays and 131
runtime typed entries. The ranked worklist is down to 50 entries. Across the
configured owner graphs, 3,572 of 3,689 memberships are semantically resolved
(96.8284%); after deduplicating shared direct members, 1,172 of 1,221 entries
are resolved (95.9869%). Three of eight source graphs are closed, while only
`game_state_update` has passed runtime import acceptance.

The latest producer iteration promotes 100 target-wide bodies and corrects
the two previously quarantined hard-float animation wrappers. Their incoming
`s0` animation speed is now an explicit C argument and target-backed caller
speeds are preserved. Removing the obsolete quarantine resolves four owner
memberships and two unique direct identities. Renderer initialization remains
source-closed at 177/177, Camera is 275/284, the two cutscene owners are
433/442 and 426/435, and the two Player owners are 1,044/1,089 and
1,045/1,090 when accepted service boundaries are included.

`actor_update_all` and `game_state_renderer_initializer` are both complete
source candidates. Runtime activation still requires their independent,
content-addressed work orders; source closure does not approve either owner.

## Current transfer: Actor_Destroy

Target entry: `0x002D644C`

Target span: `0xA4` bytes through `0x002D64EC`

SHA-256:

```text
12a0c22c64206998f326a3f220f8e401e707b89bd0e6756dc3e3108c5a6b2a21
```

The allocator-global literal at `0x002D64F0` is `0x0055A1A8`. The semantic
contract is:

```c
void Actor_Destroy(Oot3dActor* actor, Oot3dPlayState* play) {
    if (actor->destroy != NULL) {
        actor->destroy(actor, play);
        actor->destroy = NULL;
    }

    if (actor->modelContext != NULL) {
        gOot3dActorAllocator->vtable->destroy(
            gOot3dActorAllocator, actor->modelContext);
        actor->modelContext = NULL;
    }

    for (size_t i = 0; i < 6; ++i) {
        if (actor->ownedModelSlots[i] != NULL) {
            actor->ownedModelSlots[i]->vtable->destroy(
                actor->ownedModelSlots[i]);
        }
        actor->ownedModelSlots[i] = NULL;
    }

    if (actor->lastOwnedModel != NULL) {
        actor->lastOwnedModel->vtable->destroy(actor->lastOwnedModel);
    }
    actor->lastOwnedModel = NULL;
    actor->destroyState = 2;
}
```

The names for the resource types are provisional; the order, offsets and
vtable slots are target-proven:

- actor destroy callback at `Actor+0x138`;
- model context at `Actor+0x178`, destroyed through allocator vtable `+0x10`;
- six resource pointers at `Actor+0x17C..0x190`, each using vtable `+0x04`;
- a seventh resource pointer at `Actor+0x194`, also using vtable `+0x04`;
- terminal byte `2` at `Actor+0x198`, written after all callbacks return.

The correct function prototype has two arguments. At
`0x004616A0-0x004616A8`, `Actor_UpdateAll` loads `PlayState*` into `r1`,
places `Actor*` in `r0`, and calls `Actor_Destroy`. The callee leaves `r1`
untouched before dispatching `actor->destroy`, so the callback ABI is
`void(Oot3dActor*, Oot3dPlayState*)`. Do not copy Ghidra's incomplete
one-argument signature.

Consumer implementation:

- `tools/oot3d/native_game_runtime/oot3d_typed_actor_lifecycle.h`
- `tools/oot3d/native_game_runtime/oot3d_typed_actor_lifecycle.cpp`
- `tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.cpp`

Verification:

- `TestActorDestroyOwnerDifferential` follows all callback continuations and
  compares registers, flags, stack and Actor memory with generated A32;
- `TestTypedActorLifecycleCheckpointRoundTrip` saves and restores process
  state between the destroy callback and its continuation.

## Actor_UpdateAll callback boundaries

These boundaries are implemented and verified, but they do not claim that the
large `Actor_UpdateAll` body at `0x00461344` is decompiled:

| Callback | Boundary | Field | ABI | Return |
| --- | --- | --- | --- | --- |
| init | `0x004615D8` | `Actor+0x134` | `(Actor* r0, PlayState* r1)` | `0x004615E8`; clear `Actor+0x134`, continue `0x00461698` |
| update | `0x004617A4` | `Actor+0x13C` | `(Actor* r0, PlayState* r1)` | `0x004617B4` |

Exact boundary hashes:

- init plus continuation, 24 bytes:
  `cfff35edb8e2413235168a7b81a9ab1f4fc45a972685f66187b5d52e3216794f`;
- update call, 16 bytes:
  `f73d838398fe0c2a37e64da8262de291d1a9868e82e286675b584d16925041d7`.

`TestActorInstanceCallbackContractDifferential` compares init directly and
update together with its preceding timer block against generated A32. Each
callback target re-enters the normal process dispatcher: reviewed typed
targets execute as C++, while unmigrated targets retain A32 behavior.

## Actor_UpdateAll corrected boundary

The prior Ghidra function range was fused through a distant tail-call target.
Target disassembly establishes these independent ranges:

| Region | Range | Size | SHA-256 |
| --- | --- | ---: | --- |
| `Actor_UpdateAll` code | `0x00461344-0x004618F7` | `0x5B4` | `0297da8fc97ddd76a7df76a01fe8ce4c1ee8a9169ef69877ce1c903727fb49cf` |
| literal pool | `0x004618F8-0x00461903` | `0x0C` | `59b37ac341685ea466155d2f8ceaec4d7f9aa487a7d0949adaee7ead9c710966` |
| tail helper | `0x004796BC-0x00479717` | `0x5C` | `27c9725b89ad3bd3f8aebb42bd3dbd583b6c7185225abdfa4e1e5f558f2e1df3` |

`0x004618F4` is an unconditional tail branch to `0x004796BC`. The helper
copies active dynamic-collision transform records and is the OOT3D equivalent
of `DynaPoly_UpdateBgActorTransforms`; the N64 source corroborates that
semantic role but is not the target evidence. A separate function begins at
`0x00461904`.

The producer action is recorded under `resolved_structural_findings` in
`decomp_work_request.json`: split the functions, preserve the literal pool,
name the helper independently, and regenerate callgraph/function-pointer
sidecars with the helper as a direct tail dependency.

## Current transfer: shared 0x54 record initializer

Target entry: `0x002FFA20`

Target span: `0x28` bytes through `0x002FFA44`

SHA-256:

```text
4090f7eae7f7623dd52330d71676f56beeae0242e0a04fce7ffc8ac89f5d6265
```

The target initializes exactly bytes `0x00..0x4F` and returns the original
record pointer:

```c
void* Oot3d_Record54InitPrefix(void* record) {
    memset(record, 0, 0x50);
    return record;
}
```

`Oot3d_Record54InitPrefix` is intentionally provisional. The producer
callgraph still names the target `FUN_002ffa20`, and the decomp should assign a
domain name only after identifying the shared record type.

The unusual size is target-proven:

- the function explicitly clears words at `+0x48` and `+0x4C`;
- it calls `Runtime_Memzero(record, 0x48)` for `+0x00..+0x47`;
- constructor callers at `0x00418984` and `0x0041F048` both pass it to
  `FUN_00350820` with stride `0x54`;
- the final field at `+0x50..+0x53` is outside the initialized range and must
  not be folded into the memset without additional evidence.

The consumer implementation preserves the original call boundary rather than
replacing the memory service. Entry `0x002FFA20` dispatches
`Runtime_Memzero` at `0x00343280`, then typed execution resumes at
`0x002FFA40`. Both points are whole-AOT observable boundaries and all
continuation state remains in guest registers, stack and memory.

Consumer implementation:

- `tools/oot3d/native_game_runtime/oot3d_typed_record_initializer.h`
- `tools/oot3d/native_game_runtime/oot3d_typed_record_initializer.cpp`
- `tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.cpp`

Verification:

- `TestRecordInitializerDifferential` compares entry and continuation against
  generated A32, including all registers, CPSR, stack, initialized bytes and
  the preserved four-byte tail;
- `TestTypedRecordInitializerCheckpointRoundTrip` captures state at the
  memzero return and proves deterministic continuation after restore;
- invalid guest memory retains the A32 fallback without mutating registers.

Producer action:

1. Replace
   `oot3d_recovered_002ffa20_FUN_002ffa20` with reviewed semantic source.
2. Correct its pointer ABI and the callback type consumed by
   `FUN_00350820`.
3. Preserve the `0x50` initialized extent and `0x54` caller stride.
4. Identify the field at `+0x50` and the common record type from all target
   callers before assigning a final domain name.
5. Regenerate callgraph and function-pointer sidecars after promotion.

## Current transfer: AudioRequestFlag100 callback

Target entry: `0x00465304`

Target body: `0x64` bytes through `0x00465364`

Body SHA-256:

```text
aabf371b94456f19be4f1680fa1489858ac3c06d853247cd5a34446bf844a905
```

The request-state literal at `0x00465368` is `0x0054ABD4`. The body plus
literal SHA-256 is
`8127595e8272d70b4048fa355bc19401fd289289401e2cd8674afef92b97d35a`.
The sole concrete function-pointer producer in the current sidecar is
`Oot3d_AudioRequestFlag100` at `0x004536A4`, which stores the requested SFX ID
at request-state offset `0x28` before registering this callback.

Provisional semantic body:

```c
void Oot3d_AudioRequestFlag100Callback(
        RendererObjectRef* descriptor,
        uint32_t unused1,
        uint32_t unused2,
        RendererObjectHandle initialHandle) {
    RendererObjectHandle handle = initialHandle;
    RendererObjectRef_Assign(&handle, descriptor);

    int32_t objectSfxId =
        descriptor->object != NULL ? descriptor->object->sfxId : -1;
    if (objectSfxId == gAudioRequestFlag100State.requestedSfxId &&
        (handle == 0 || Oot3d_AudioRequestStatus(handle) <= 12U)) {
        gAudioRequestFlag100State.pending = 0;
    }

    RendererObjectRef_Clear(&handle);
}
```

Target-proven layout and behavior:

- `RendererObjectRef.object` is at `+0x00`;
- the referenced object's SFX ID is at `+0x9C`;
- request-state `pending` is the byte at `+0x04`;
- request-state `requestedSfxId` is the word at `+0x28`;
- a null referenced object supplies the sentinel SFX ID `-1`;
- status comparison is unsigned and accepts values `0..12`;
- assign, status-query and clear remain calls to the original guest services.

The entry saves `r3-r5/lr`, then passes the address of the saved `r3` word to
`RendererObjectRef_Assign`. That stack word is also the callback's local
handle. Consequently, `r3` restores the final handle rather than the incoming
value. This stack alias is part of the target ABI and must not be optimized
away without proving that all callers treat `r3` as volatile.

A historical imported semantic tranche labels `0x00465304` as
`PauseDungeonItem_AssignOptionalObjectRef`. That label conflicts with the
concrete `Oot3d_AudioRequestFlag100` pointer producer and its SFX request
state. Do not propagate the pause classification without new target evidence.

Consumer implementation:

- `tools/oot3d/native_game_runtime/oot3d_typed_audio_request_callback.h`
- `tools/oot3d/native_game_runtime/oot3d_typed_audio_request_callback.cpp`
- `tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.cpp`

Verification:

- `TestAudioRequestFlag100CallbackDifferential` covers mismatched IDs, a null
  handle, status boundaries `12` and `13`, the null-object sentinel and
  non-destructive guest-memory failure;
- `TestTypedAudioRequestCallbackCheckpointRoundTrip` captures MessagePack
  state at the status return, restores it and verifies the final stack,
  registers and request-state byte;
- all four internal callback boundaries are observable typed entries, while
  the three called guest services keep their existing implementation.

Producer action:

1. Replace `oot3d_recovered_00465304_FUN_00465304` with reviewed semantic
   source in the audio owner.
2. Type the callback slot used by `Oot3d_AudioRequestFlag100`.
3. Preserve the exact offsets, unsigned status threshold and `r3` stack alias.
4. Resolve final names for the object-reference and handle types from their
   wider users.
5. Remove or explicitly supersede the stale pause-item classification.
6. Regenerate function-pointer and callgraph sidecars after promotion.

## Current transfer: GameState_Update

Target entry: `0x00417014`

Target bytes:

```text
10402de9041090e50040a0e131ff2fe1f80094e5010080e2f80084e51080bde8
```

SHA-256:

```text
cd3896654716c877313830f1ff251959e8ba219d7cdfdfd9d578fb4a62e4895a
```

Recovered semantics:

```c
void GameState_Update(GameState* gameState) {
    gameState->main(gameState);
    gameState->frames++;
}
```

Required layout:

- `GameState.main` is a `void (*)(GameState*)` callback at `0x04`;
- `GameState.frames` is a 32-bit counter at `0xF8`;
- the callback completes before the counter is incremented;
- the callback is generic to every `GameState`, not specific to `PlayState`;
- this OOT3D body does not contain the framebuffer/input work present around
  the analogous N64 control flow.

Consumer implementation:

- `tools/oot3d/native_game_runtime/oot3d_typed_game_state.h`
- `tools/oot3d/native_game_runtime/oot3d_typed_game_state.cpp`
- `tools/oot3d/native_game_runtime/oot3d_typed_gameplay_bridge.cpp`

Verification:

- `TestGameStateUpdateOwnerDifferential` compares entry, callback return,
  registers, CPSR, guest stack, counter wrap and failure fallback with the
  generated A32 registry;
- `TestTypedGameStateCheckpointRoundTrip` serializes process state at the
  callback boundary and proves deterministic continuation after restore.

The decomp commit `d1f8b1d907a7e1b8d823abde58e94e4d900dc5dc` still carries
this function only as `oot3d_recovered_00417014_GameState_Update` in the bulk
recovery cohort. It had not yet superseded this transfer when this document was
written.

## Current transfer: Actor/Player structural microleaves

Three direct static dependencies now have target-exact typed implementations:

| Entry | Producer symbol | Suggested semantic name | Target bytes | Owner effect |
| --- | --- | --- | ---: | --- |
| `0x00334354` | `FUN_00334354` | `Player_ReleaseLockOn` | 28 | Actor and both Player closures |
| `0x0047C938` | `FUN_0047c938` | `ActorUpdateRecord_InitializeDefaults` | 40 | Actor closure |
| `0x0047CCDC` | `FUN_0047ccdc` | `ActorUpdateRecord_ClearHalfwords` | 16 | Actor closure |

`Player_ReleaseLockOn` clears the actor pointer at `Player+0x16F8` and bit
`0x2000` in the word at `Player+0x1714`. Its exact machine ABI leaves `r0`
at `player+0x1000` and `r1` at the masked flags value.

`ActorUpdateRecord_InitializeDefaults` clears bytes `+0x18..+0x1B` and writes
the VFP literal loaded from `0x0047C960` to words `+0x04`, `+0x08` and
`+0x0C`. The consumer deliberately reads the literal from guest memory. Its
differential test replaces the native zero with a nonzero bit pattern, which
prevents an apparently equivalent hardcoded-zero implementation from passing.

`ActorUpdateRecord_ClearHalfwords` performs exactly two 16-bit clears at
`+0x04` and `+0x0C`; surrounding bytes are compared against A32 so widened
stores fail verification.

Implementation:

- `tools/oot3d/native_game_runtime/oot3d_typed_player_lock_on.*`;
- `tools/oot3d/native_game_runtime/oot3d_typed_actor_update_records.*`;
- dispatch and telemetry in `oot3d_typed_gameplay_bridge.*`.

Verification:

- `TestActorOwnerMicroleafDifferential` compares all integer registers,
  CPSR, FPSCR, VFP state and affected guest memory with generated A32;
- `TestTypedActorOwnerMicroleafCheckpointRoundTrip` serializes and restores
  guest process state through MessagePack, then executes all three leaves;
- invalid read/write cases retain A32 without modifying guest state.

The complete structured ABI, target bytes, hashes, layouts and proposed
producer bodies are in `decomp_handoff.json`. Producer symbols remain `FUN_*`
until the producer explicitly accepts the suggested semantic names.

## Current transfer: PauseUi_UpdateDualAlpha

The direct `Actor_UpdateAll` child at `0x0047955C` is now reconstructed as
`PauseUi_UpdateDualAlpha`. Its two direct dependencies,
`PauseContext_GetState` and `Math_StepToS`, were already producer-reviewed;
therefore this promotion closes the complete three-node subtree without
claiming ownership of either service.

The native contract uses the state pointer in `r1`, byte timers at `+0x10` and
`+0x11`, and signed alpha values at `+0x12` and `+0x14`. It queries pause state
before reading the timers. The timer phase selects either target zero or 255,
then derives both step sizes from the signed update rate reached through
pointer cell `0x0051B2F4`.

The runtime implementation is isolated in
`oot3d_typed_pause_ui_alpha.*`. It reads the update pointer, bias and four
factors from the original literal pool at `0x004796A4..0x004796BB`; it does
not substitute host timing constants. Entry, pause-query return and both
first-step returns are guest-visible continuations. The second
`Math_StepToS` remains a tail call, matching the target.

`TestPauseUiUpdateDualAlphaDifferential` covers pause blocking, gate hold and
expiry, fade-in, fade expiry and fade-out while comparing all ARM/VFP state,
timers, alpha fields and the 24-byte frame with generated A32.
`TestTypedPauseUiAlphaCheckpointRoundTrip` captures and restores MessagePack
state between the two alpha calls. Invalid memory paths are non-destructive.

## Current transfer: DynaPoly_ResetActorInteractFlagsIfRegistered

The direct `Actor_UpdateAll` leaf at `0x0047AF24` scans the 50 native dynamic
collision slots and clears `Oot3dDynaPolyActor.interactFlags` at `+0x1B8`
only when the updated Actor is still registered in an active, non-deleted
slot.

The target uses two distinct registry views:

- delete/active halfwords at `dynaContext+0x151C`;
- actor flags at `play+0x0A98+0x156C`;
- actor records at `play+0x0A98`, stride `0x6C`, pointer at `+0x54`.

The 140-byte body has SHA-256
`7fc90e708c31f6860e1d824077fd43dcded8d96979f6b597afbded331d94842a`.
Ghidra fused its conditional 12-byte tail at `0x00483C9C`; that tail has
SHA-256
`08c1159363a108d353d072c2ca64bb567aa7fbc1642e06e06ba1396a29859b8a`.
The discontiguous concatenation has SHA-256
`ca2873d77a26ff4b2b5ea889604d29d2645c77ffac4593e155b368d34951a635`.
It must not be represented as a contiguous 36-KiB function.

The runtime replacement is isolated in
`oot3d_typed_dyna_interaction_reset.*`. It preserves the exact ARM side
effects, including the different `lr` values on match and miss, and owns no
host continuation state. `TestDynaInteractionResetDifferential` compares
seven slot configurations, all registers, VFP state, CPSR, stack and affected
memory against generated A32. The MessagePack test restores the complete guest
state before executing the leaf. Read and write failures retain A32 without
mutating guest state.

OOT3D code, its existing `DynaPoly_DeleteBgActor` source and
`Oot3dDynaPolyActor` layout are authoritative. N64
`func_8003F8EC`/`DynaPolyActor_UnsetAllInteractFlags` only corroborate the
gameplay intent.

## Historical producer directive at 294123b

This section is retained as provenance for the tranche that led to the Actor
closure. Its counts and action queue are superseded by `Current Actor
frontier` below and must not be used as the current worklist.

Between the former `d1f8b1d9` checkpoint and `294123b0`, the producer adds 71
reviewed target-linked bodies. Fifteen are newly reviewed members of the
configured owner closures, reducing the consumer worklist from 927 to 912.
The useful owner-facing promotions are:

- `FUN_00350820`, a target-native strided callback loop shared by every
  blocked owner;
- `Player_SetEquipmentData`, `Player_SetBootData`,
  `Camera_CalcAtDefault` and hard-float vector-distance leaf
  `FUN_00338a90`;
- `Message_ContinueTextbox` and audio wrappers `FUN_0036aeb4` and
  `FUN_00371af0`;
- `GetAppletType`, `TryGetFileSize`, `glGetError`, `FUN_00338c04`,
  `FUN_0036ae48`, `FUN_0036e980` and `FUN_0037073c`.

These bodies close source-graph entries, but runtime import still requires
their native contracts. In particular:

- `FUN_00350820` invokes an address supplied in `r1`; the source importer must
  route it through the typed-or-A32 callback boundary rather than cast a
  target PC to a host function pointer;
- `TryGetFileSize` dispatches vtable slot `+0x08` and requires the same target
  pointer treatment;
- `FUN_00338a90` returns through hard-float register `s0`;
- `FUN_0037073c` ends as a tail call to
  `oot3d_message_textbox_common`;
- equipment and boot data must retain OOT3D globals, offsets and model
  selection rather than inherit N64 layouts.

The 1,050 `semantic_certified_bulk` entries are useful evidence and a cleanup
queue, not closed source. Eighteen intersect configured owners. The first
producer closure tranche should graduate the owner-relevant candidates in
this order:

1. Confirm the compiler-runtime family rooted at `0x00332754`, described
   below, and remove it from gameplay ownership.
2. Absorb the nine entries in `decomp_handoff.json`; none is yet superseded
   by producer-reviewed source. `Actor_Destroy` and `GameState_Update` being
   architecture-certified does not supersede target-differential consumer
   implementations.
3. For Actor, review `Actor_Delete`, `Player_InCsMode` and
   `Math3D_TriChkPointParaX/Y/Z` from target instructions.
4. For cutscenes, review `Message_ShouldAdvance`, `Item_Give` and the shared
   `Player_SetModels`.
5. For the environment/renderer owner, review
   `LightContext_InsertLight`.
6. Continue through the remaining owner-local certified entries before
   generating additional unrelated architecture-scaffold tranches.
7. Finish the owner roots themselves and resolve their indirect edges only
   after the reachable static bodies have reviewed semantic source.

Use this strategic owner order:

| Owner | Closure | Producer | Consumer | Missing | Indirect | Manual | Next action |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `game_state_update` | 1 | 0 | 1 | 0 | 0 | 0 | Absorb the accepted consumer owner into producer source. |
| `actor_update_all` | 173 | 59 | 7 | 107 | 0 | 1 | Resolve the runtime family, then close `0x00477E44`. |
| `game_state_renderer_initializer` | 177 | 38 | 1 | 138 | 0 | 0 | Promote `LightContext_InsertLight`, then work vertically. |
| `cutscene_process_commands` | 437 | 109 | 1 | 326 | 0 | 0 | Share promotions with the frame owner. |
| `cutscene_frame_owner` | 444 | 109 | 1 | 333 | 0 | 0 | Close together with command processing. |
| `camera_update` | 284 | 107 | 1 | 176 | 35 | 0 | Finish static source before resolving the 35 indirect targets. |
| `player_update_common_candidate` | 1,089 | 320 | 2 | 766 | 97 | 1 | Verify root identity; take shared leaves opportunistically. |
| `player_update` | 1,090 | 320 | 2 | 767 | 97 | 0 | Defer full closure until the smaller owners and shared leaves land. |

Counts are closure membership, not independent work estimates: many bodies
are shared. `actor_update_all` is the first practical blocked source owner
because it has the smallest remaining graph and no indirect or dynamic edge.
The divmod request is assigned once in the report but records all three
affected paths in `affected_owner_depths`: Actor at depth 1, cutscene command
processing at depth 2 and the cutscene frame owner at depth 3.

The machine-readable open request
`classify_a32_signed_divmod_service_family` records exact spans and hashes for
the first item. Target control flow strongly indicates:

| Entry | Candidate role | Span | SHA-256 |
| --- | --- | ---: | --- |
| `0x00332754` | signed 64-bit divmod wrapper (`__aeabi_ldivmod` candidate) | 80 | `ec1af4e82dfbf9e0f5d5deb0743ef285cb1722dd8d5f49ebe42a12ffc041adda` |
| `0x00447FAC` | unsigned 64-bit divmod core (`__aeabi_uldivmod` candidate) | 688 | `c947f0888273995e7815e619f8b402a0edd6c06dd08c344e1cf9a33be4ae555f` |
| `0x002DF174..0x002DF17B` | divide-by-zero identity return (`__aeabi_ldiv0` candidate) | 8 | `406da2c395ac47763d61d17f1172502baa78e7dfbfb4149415c26053cadd1a4f` |

The signed wrapper normalizes both 64-bit operands, calls the unsigned core,
then independently restores quotient sign in `r0:r1` and remainder sign in
`r2:r3`. Ghidra currently separates the terminal `bx lr` at `0x002DF178`
from the one-instruction body at `0x002DF174`. The producer must compare the
bodies with the exact ARM EABI/compiler runtime used by the title. If the
identity is confirmed, merge that boundary, publish ABI-correct runtime
source or service symbols and regenerate both sidecars at the same commit.
Do not reconstruct this family from N64 gameplay source.

## Current Actor frontier

At `d108e7a`, `actor_update_all` is source-closed:

- 171 functions in the static closure;
- 170 producer-reviewed semantic bodies;
- one exact ARM EABI compiler-service boundary;
- zero bulk-only or certified-only bodies;
- zero unresolved indirect edges;
- zero open dynamic contracts;
- zero open manual blockers.

This is a substantial producer result, but it does not yet make the owner an
active consumer implementation. The current typed runtime registers selected
Actor lifecycle entries and the init/update callback continuations; it does
not register or execute the complete owner root at `0x00461344`. The normal
game build also does not link the materialized producer files.

The package therefore emits a separate content-addressed work order:

```text
owners\actor_update_all\runtime_import_work_order.json
```

Its state is `acceptance_required`. This accurately preserves the useful
source closure while preventing a partial callback bridge from being counted
as a complete owner import.

## Consumer Actor import procedure

1. Resolve `latest.json`, verify its manifest SHA-256 and use only the pinned
   source candidate recorded by the Actor work order.
2. Compile the candidate in an isolated consumer target. The normal build
   must not inspect or depend on `I:\oot3decomp`.
3. Map producer pointer/global resolver macros onto `NativeA32Memory`, with
   checked guest-to-host and host-to-guest address conversion.
4. Route target-address callbacks through the existing typed-or-A32
   dispatcher. Never cast a guest address directly to a host function pointer.
5. Differential-test the complete owner root against Native30, including all
   actor categories and init, update, destroy and owned-resource callbacks.
6. Round-trip process state at every callback boundary and resume both paths.
7. Run a Kokiri savestate smoke with the complete owner enabled and retain an
   explicit A32 fallback switch during acceptance.
8. Classify Enhanced60 timing only after Native30 state and callback
   equivalence pass.
9. Add a hashed `owner_import_acceptance` record and then, and only then, add
   `actor_update_all` to `approved_owners`.

## Remaining producer frontier

`game_state_renderer_initializer` is now producer-closed: all 177 reachable
entries are reviewed semantic source, including root `0x0044E7C0` and bounded
alternate entry `0x00302BB8`. Its next work is consumer-side acceptance:
guest-memory and callback adapters, typed-or-A32 fallback, Native30
differential tests, savestate round trips and a hashed import decision.

Keep the producer residual as one cross-owner macro-tranche: 50 ranked
entries, comprising 49 unique direct-closure identities and one shared
indirect target. It contains 47 bulk-only bodies, two semantic-certified
bodies and one exact-anchor-only identity; no source promotion remains
quarantined.
Prioritize the owner roots, the two certified bodies and shared runtime/hot
dependencies before isolated cold leaves. Effective residuals are nine for
each cutscene owner, nine plus the shared indirect target for Camera, and 45
plus that same indirect target for each Player owner. The stale Player-common
root identity remains an explicit manual blocker.

An owner is producer-closed only when:

- its root and every reachable static body are reviewed semantic source or an
  exact, named external/compiler service boundary;
- no `semantic_certified_bulk` or `bulk_only` body is counted as closed;
- every consumer overlay in its closure has been absorbed or superseded by
  target-reviewed producer source;
- every indirect target, dynamic callback, vtable call and manual blocker is
  resolved with its ABI recorded;
- callgraph and function-pointer sidecars were regenerated from the same
  committed revision as the source;
- producer policy tests and focused target/ABI tests both pass.

Only after source closure should the runtime evaluate the separate
`owner_import_acceptance` gate. Source closure alone does not enable an owner.

## Evidence rules

- OOT3D target instructions and native data layouts are authoritative.
- N64 source may explain intent, but it cannot override OOT3D ABI, offsets,
  timing or backend behavior.
- Generated sidecars are valid only for the checkout revision recorded beside
  their hashes. A package can intentionally pin older source while observing
  newer sidecars, but it must report that mismatch and cannot claim exact
  historical topology.
- A bulk-recovered body is evidence, not reviewed semantic source.
- Consumer and producer implementations may differ structurally when both
  reproduce the same target contract.

## Current b2263fa producer frontier

The current immutable package is:

```text
I:\oot3dre_work\source_integration\b2263fafb2dd-7286a857de90
```

Producer commit `b2263fafb2dd23288456c97cef5c9c1632d8ff86` promotes 100
reviewed target bodies. Ten intersect the configured owner closures and
resolve 26 weighted memberships; 90 advance the target-wide registry. The
ranked producer worklist falls from 20 to ten unique direct identities
(-50.0%) and contains no indirect, dynamic or quarantined request.

Weighted semantic closure is 3,667/3,689 (99.4036%) and unique direct closure
is 1,211/1,221 (99.1810%). Effective closure is:

| Owner | Closed | Total | Coverage | Unique gaps |
| --- | ---: | ---: | ---: | ---: |
| `player_update` | 1,082 | 1,090 | 99.2661% | 8 |
| `player_update_common_candidate` | 1,081 | 1,089 | 99.2654% | 8 |
| `actor_update_all` | 171 | 171 | 100.0000% | 0 |
| `cutscene_frame_owner` | 441 | 442 | 99.7738% | 1 |
| `cutscene_process_commands` | 434 | 435 | 99.7701% | 1 |
| `camera_update` | 280 | 284 | 98.5915% | 4 |
| `game_state_update` | 1 | 1 | 100.0000% | 0 |
| `game_state_renderer_initializer` | 177 | 177 | 100.0000% | 0 |

The gaps overlap, so these rows represent only ten distinct producer
functions and 22 weighted owner memberships. Cutscene has only
`Cutscene_ProcessCommands` left. Camera has `Camera_Update` plus three shared
services. Player and Player-common share their eight residual functions.

Source closure remains 3/8 (37.5%) and runtime-import readiness remains 1/8
(12.5%). This distinction is intentional: Actor and the renderer initializer
have complete source graphs but still require the separate consumer
acceptance procedure; GameState is the only accepted runtime owner.

The next producer wave should treat all ten identities as one final owner
frontier. Highest priority is the Cutscene root, then the Camera root, the
stale Player-common root identity, the shared BgCheck pair and the remaining
Player services. Consumer runtime-import work may continue independently and
must not convert source coverage into an implicit acceptance.

## 2026-07-29 final shared-leaf checkpoint

The current immutable consumer baseline is:

```text
I:\oot3dre_work\source_integration\449e219e8a8b-a6b96b41f301
```

It pins producer commit `449e219e8a8bdd8025981b0ba5f961f1a6614ea4`
and ignores the producer's active worktree. Relative to `f95d2875`, it contains
104 new reviewed semantic promotions. Four intersect the configured owner
frontier:

- `0x002BBF74`, the static floor-list traversal;
- `0x0032DB24`, `BgCheck_RaycastFloorImpl`;
- `0x00342C10`, `EffectSs_Spawn`;
- `0x00465534`, the audio runtime mix update.

Together with the `0x003555D8` hard-float C99 `sinf` service boundary, these
reduce the unique owner worklist from nine to five identities. Owner
membership coverage is now 3,675/3,684 (99.7557%) and unique closure coverage
is 1,215/1,220 (99.5902%). The three source-closed owners and the single
runtime-approved owner are unchanged: producer source maturity is not runtime
acceptance.

The remaining producer frontier is exact:

- `0x00250AD0`, whose stale Player-common identity still needs target proof;
- `0x002C5BA0`, `Cutscene_ProcessCommands`;
- `0x002D84C4`, `Camera_Update`;
- `0x0046C204`, a hot Player dependency;
- `0x004991B4`, a shared Player dependency.

The consumer treats `0x00461344` canonically as `Actor_UpdateAll` and
`0x0044E7C0` canonically as `Actor_InitContext`, even while retaining their raw
callgraph names in diagnostic output. `game_state_renderer_initializer`
remains only as the stable historical owner ID. Its old label described the
conspicuous renderer-resource tail but not the complete function, which also
clears the 0x20C ActorContext, initializes actor categories, spawns Player and
installs the context light.

The target boundary is also explicit. The contiguous root is
`0x0044E7C0-0x0044EFA7` (2,024 bytes, SHA-256
`09df5dfd35274cd1f493071bbe2c87cfc5e90ef2143f4dfed73ab0b1b112ebd4`).
After restoring its frame, `0x0044EFA4` tail-branches to an independently
framed helper at `0x004632B4-0x0046341F` (364 bytes, SHA-256
`55824fd0e8c3c6989a61eb9a58e596d3dfb5f2424da490de04fa4a9795b4a3ac`).
The 85,088-byte Ghidra extent is therefore not contiguous owner code. A
producer split should preserve the helper as a direct tail dependency and
regenerate function extents and callgraph sidecars.

`NativeA32Memory::GetGuestAddress` now provides the checked host-to-guest half
of the source-owner pointer adapter. It accepts only a non-empty range wholly
owned by one mapped region and rejects null, cross-boundary and foreign-memory
pointers. `oot3d_native_a32_process_tests` covers read-only storage, copied
memory and ownership rejection. This is infrastructure for both
`Actor_UpdateAll` and `Actor_InitContext`; it does not activate either owner.

`NativeA32Process::InvokeFunctionWithState` and
`NativeA32OwnerCallAdapter` now complete the dispatcher half of that boundary.
An owner supplies a caller-derived `GuestState`, exact source-owner frame
size, core/VFP arguments and stack words. The nested call executes through
the existing typed-or-A32 dispatcher against shared guest memory while the
scheduler-owned thread registers remain untouched. The adapter rejects nested
source-owner scopes, foreign pointers, unaligned stacks and stack arguments
outside the declared owner frame. Its process test enters an owner from the
native callback, recursively dispatches a guest callee, and verifies core,
VFP, stack and return-state propagation. This is import infrastructure only:
`0x00461344` remains disabled until the complete Native30 and callback
round-trip acceptance passes.

## 2026-07-29 Actor_UpdateAll consumer oracle

The pinned `Actor_UpdateAll` candidate is now copied under
`tools/oot3d/source_imports/actor_update_all_449e219e`. Its import manifest
records the producer revision, immutable package IDs and SHA-256 for every
source file. An `EXCLUDE_FROM_ALL` C object target compiles this exact source
without adding it to normal game builds.

`oot3d_source_actor_update_all_tests` executes the complete source root and
the original generated A32 root against independently initialized but
byte-identical `NativeA32Process` instances. The current Native30 sequence
covers:

- three consecutive authored ticks;
- all twelve populated actor categories in exact traversal order over three
  ticks, with deferred `init` followed by two `update` callbacks per actor;
- the exact `0x004615E8` init and `0x004617B4` update continuations;
- checked guest pointer encode/decode and nested guest callback dispatch;
- SVC `0x28` count and return-value equivalence;
- nonresident kill, drawn-actor destroy and deleted-actor unlink/resource
  release, including execution of the original A32 `Actor_Delete`;
- byte comparison of Play, Player, embedded ActorContext, every fixture Actor,
  overlay/resource state and all touched owner globals after every tick;
- a process capture, destructive mutation and restore after the first tick;
- independent capture, destructive mutation and repeatable restore at the
  first init and update callback boundaries on both execution paths.

The differential also fixes two layout facts that must be preserved by future
producer types: `ActorContext` is embedded at `PlayState+0x208C`, and
`PlayState+0x20AC` is both the Player pointer and the category-2 list head.
The Player fixture is therefore the category-2 Actor rather than a duplicate
allocation.

This exposed and fixed a dispatcher defect: a synchronous nested call whose
return PC was itself a registered A32 block continued executing that block.
`InvokeFunctionWithState` now composes the installed native callback with an
exact return-PC trap, and its process regression test proves that the return
block is not entered.

That oracle was the pre-activation checkpoint. Production activation and its
runtime evidence are recorded in the following section.

## 2026-07-29 Actor_UpdateAll production bridge

`oot3d_source_actor_update_all_runtime` now owns the complete consumer bridge.
All direct dependencies are dispatched at their exact guest addresses through
`NativeA32OwnerCallAdapter`; init/update function pointers use their original
`0x004615E8` and `0x004617B4` continuations; SVC `0x28` is handled by
`NativeA32Process::InvokeSvcWithState`. Output pointers use checked guest
stack scratch frames matching the original ABI. The game does not contain
test service thunks.

Activation is explicit with `--enable-source-actor-update-all` or the
`-EnableSourceActorUpdateAll` launcher switch. It is off by default, so an
unaccepted source owner cannot silently replace the A32 route. A failure
before guest mutation declines source selection; a failure after owner
mutation produces a controlled unsupported fault instead of replaying the
A32 owner over partially modified state.

Verification completed in the consumer:

- `oot3d_native_a32_process_tests`: pass;
- `oot3d_source_actor_update_all_tests`: pass;
- Kokiri Native30 smoke: 60 frames, 59 owner calls, 19,334 nested guest
  calls, 3,177 update callbacks and 59 SVC calls;
- bridge failures, whole-AOT memory faults and unsupported exits: zero.

The smoke report is
`I:\oot3dre_work\native_game\actor_update_all_source_smoke.json`, SHA-256
`d7052debf18b38c953b34c5c56ae811a82e59589cd8ce86ee41d9fd4ff102242`.
Its `source_actor_update_all` object is the machine-readable acceptance
record. The import manifest now labels runtime activation
`experimental_opt_in_native30_accepted`.

No producer source change is requested by this integration. The remaining
consumer work is longer Native30 interaction coverage, Enhanced60
classification and reuse of this bridge for `Actor_InitContext`.

## 2026-07-29 eight-owner closure consumer findings

Producer commit `d5c292708f7f25f921a066449db3cea43fad73c4` is verified clean
and closes all eight configured owner graphs. The consumer pins snapshot
`d5c292708f7f-3819f4804b32`; no further owner-closure tranche is requested.
Future producer work should correct source-level ABI expression and improve
types without reopening the completed closure graph.

The first complete `Actor_InitContext` differential exposed target facts that
should be folded back into producer source:

| Target site | Current producer expression | Required source contract |
| --- | --- | --- |
| `0x0044E8B8-0x0044E8FC` | `Actor_Spawn(...)` with an unspecified declaration | Typed hard-float prototype: `s0-s2` position, `r0-r3` actorContext/play/id/rotX, four integer stack words. |
| `0x0044EDF8-0x0044EE28` | Six-argument `Lights_PointNoGlowSetInfo(...)` | Nine logical arguments: info, `float x/y/z`, RGB, radius and attenuation. Y is the stored attention Y plus literal `80.0f` at `0x0044EFA8`. |
| `0x0044EEB4-0x0044EEC8` | `FUN_00348be4()` | Pass the object returned by `FUN_00348f34` explicitly; target retains it in `r0`. |
| attention byte-to-float and signed coordinate stores | `undefined4` temporary assigned from `Vector*ToFloat` | Store via typed `float` lvalues, matching target `vcvt` plus `vstr`; do not perform a C float-to-`u32` numeric conversion. |

The consumer implementation is isolated in:

```text
tools/oot3d/source_imports/actor_init_context_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_actor_init_context_runtime.*
tools/oot3d/native_game_runtime/oot3d_source_actor_init_context_tests.cpp
```

The differential passes with both preinitialized and acquired renderer
guards. It compares the entire mapped owner data and game RAM after Player
spawn, light insertion, all 29 renderer factory calls, allocator/model setup,
four materials and the `0x004632B4` tail helper. It also restores checkpoints
captured before Player spawn and after resource construction. This evidence
approves the consumer lowering, not the current producer C spellings listed
above.

Producer status is now 8/8 source-closed. Consumer runtime status is 2/8
accepted (`GameState_Update`, `Actor_UpdateAll`), with
`Actor_InitContext` differential-complete and process-image smoke pending.

### Runtime acceptance boundary

The available frame-10380 Kokiri preload checkpoint reaches deferred
`ObjHana_Init` (`0x001E1734`) through `Actor_UpdateAll` before it can provide
process evidence for `Actor_InitContext`. The callback is present in the
producer snapshot as a compile-qualified recovered function, but is outside
the eight-owner runtime package and the current executable has no linked
whole-AOT fallback. This is a consumer composition boundary, not a reopened
owner-closure request.

The consumer bridge now converts any nested guest-call failure into a
controlled `Unsupported` result carrying the precise failing guest address.
A regression forces such a dependency failure and verifies that imported C
cannot continue into a null host dereference.

The later frame-10420 checkpoint completes 120 Native30 presentation frames
with 119 source `Actor_UpdateAll` calls, 39,428 nested guest calls, 6,624
update callbacks and no failure, but observes zero `Actor_InitContext` calls.
Report:
`I:\oot3dre_work\native_game\actor_init_context_source_smoke_10420.json`;
SHA-256
`07c5af58e937e3b537a353c28942c85d5b83f09474320bbefb16791e977430ba`.
It must not be used to promote `Actor_InitContext`.

## 2026-07-29 ObjHana callback composition closeout

The frame-10380 boundary above is now resolved without reopening any of the
eight producer owner closures. The consumer pins:

```text
tools/oot3d/source_imports/obj_hana_init_d5c2927
```

Target evidence:

| Body | Range | Size | SHA-256 |
| --- | --- | ---: | --- |
| `ObjHana_Init` | `0x001E1734-0x001E1817` | 228 | `bfeba6d594858a59a92cc19ef22e4f72dfc6e08b33b38ea53aa89cd64c0af801` |
| `Actor_LoadModelList` | `0x00372F38-0x0037305B` | 292 | `0bba419749c9f7ed9f20e8cfdb2a755dce7d4c9e8bd4c565e188e0259d2411bc` |

The source callback preserves the target model-record selector, object-slot
archive lookup, renderer guard, factory context lifetime, model statistics,
hard-float scale ABI, collider initialization and save-context event bit.
The `Actor_LoadModelList` transfer calls the original target
`ZAR_GetCMBByIndex` and target virtual factory; it does not replace archive
or model formats.

The differential compares all mapped owner memory for selectors 0, 1 and 2.
It separately proves the original A32 model-loader boundary and the source
loader's ZAR/factory sequence, and retains controlled failure on an injected
dependency error.

The target CMB setup reached by ZAR uses finite 100 microsecond
`SleepThread` calls. This required a consumer runtime contract, not a producer
source rewrite: synchronous owner calls may now complete a registered finite
CTR sleep by advancing the emulated deadline. Infinite or externally
signalled waits remain scheduler-only. The mechanism is generic and contains
no ObjHana/CMB address test.

Process evidence:

```text
I:\oot3dre_work\native_game\actor_init_context_source_smoke_10380_objhana.json
SHA-256 44dcac6bea9a601cc0f0bde8b412bcea44181d0b9f3f5cf23b25d932f63ac8ef
```

The run completes 120 Native30 frames with 119 source `Actor_UpdateAll`
calls, 36,056 nested guest calls, one source `ObjHana_Init`, 150 owner SVC
calls and zero source-owner or callback failure.

Producer follow-up is limited to source quality: retain the variadic
terminator and hard-float ABI explicitly when refining these two recovered
bodies. No owner-closure work is requested. Consumer runtime status remains
2/8 accepted roots; `Actor_InitContext` is differential-complete and now
requires a checkpoint captured at its `0x0044E7C0` entry for independent
process acceptance.

## 2026-07-29 Actor_InitContext runtime closeout

The requested process checkpoint is now available and the owner is accepted
as an explicit Native30 opt-in. The frame-12000 run reaches
`Actor_InitContext` once and completes 600 refreshes with zero source-owner
failure. Its report is:

```text
I:\oot3dre_work\native_game\actor_init_context_source_smoke_frame12000_accepted.json
SHA-256 bab3cbe1e70352747c4ac5a472dbae6f3efab3f0737ddf76cdd101b35d2e886a
```

Formal consumer acceptance is recorded in
`I:\oot3dre_work\source_integration\d5c292708f7f-4adc6afbb839`.
The package remains pinned to producer commit `d5c2927`, reports all eight
source graphs closed and hashes three runtime-ready owner records.

The producer should incorporate one additional ABI correction when refining
the closed source:

| Target site | Required correction |
| --- | --- |
| `0x0044EF54 -> 0x0034897C` | Declare and pass four core arguments. The call site explicitly sets `r2=0` and `r3=0`; the fourth argument controls optional renderer transfer-state allocation. |

The previous three-argument declaration allowed a stale caller value in `r3`
and faulted inside the optional allocation path. The consumer import now
passes both zeros and its differential asserts the complete four-register
contract.

Two runtime mechanisms required no producer gameplay rewrite:

- `NativeA32OwnerGuestCall` carries per-call execution limits, allowing the
  bounded `0x004644A8` model/material initialization loops to exceed the
  conservative default transition count without globally weakening nested
  calls;
- finite synchronous renderer sleeps may make a VBlank deadline late, so the
  display scheduler records and catches up overdue deadlines while preserving
  monotonic CTR time.

The acceptance report records one owner call, 86 nested guest calls, 29
dynamic factories, 56 direct dependencies, one allocator call, one light
setup, zero failures and exact final clock/deadline alignment. Consumer
runtime acceptance is therefore 3/8; producer source closure remains 8/8 and
does not need to be reopened.

## 2026-07-29 Cutscene_UpdateFrame differential findings

The consumer has imported the complete `FUN_00321f50` body as
`Cutscene_UpdateFrame`. Target identity is established entirely from OOT3D
evidence: it owns `CutsceneContext+0x20`, performs the backend-clock handshake
through `0x002C2D78` and `0x0048B198`, and calls
`Cutscene_ProcessCommands@0x002C5BA0` with
`*(PlayState+0x229C)`.

Target body:

```text
0x00321F50-0x00322073
292 bytes
SHA-256 bb317ded32373e22fc7aa7b775eb794455808f381868228c7dbe904f55ec3430
```

The consumer loads the five literal cells at `0x00322074-0x00322084` from
the mapped target image. The backend tick conversion preserves signed
integer-to-float conversion, the target `0x38000000` and `0x41F00000`
multipliers, signed truncation and `uint16_t` frame writes. Producer
refinement should retain those types explicitly; this path must not be
rewritten as a host-duration or 60 Hz policy.

Differential coverage is complete for all observed branches, including
multi-call parser catch-up and checkpoint restore/replay. A value above
`2^24` verifies rounding and exception flags through software-exact VFP
operations, including FPSCR propagation into nested guest calls and back to
the owner caller. Implementation and evidence live in:

```text
tools/oot3d/source_imports/cutscene_update_frame_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_cutscene_update_frame_runtime.*
tools/oot3d/native_game_runtime/oot3d_source_cutscene_update_frame_tests.cpp
```

No producer closure is reopened. The remaining work is consumer composition:
an explicit Native30 process opt-in for this owner, followed independently by
promotion of the much larger `Cutscene_ProcessCommands` owner. Enhanced60
classification remains outside source-owner correctness.

## 2026-07-29 Cutscene_UpdateFrame runtime closeout

The consumer composition step is complete. The owner is available only with
`--enable-source-cutscene-update-frame` in
`native30_no_interpolation`; Enhanced60 explicitly retains the separate typed
frame-crossing policy.

The retained Link-dream run completes 300 refreshes with:

- 299 source `Cutscene_UpdateFrame` calls;
- 299 nested calls to the original `Cutscene_ProcessCommands`;
- 299 source `Actor_UpdateAll` calls and 3,872 Actor callbacks;
- zero owner failures and zero late VBlank deadlines.

The current typed baseline from the same checkpoint reaches the same final
guest tick and exact memory-content fingerprint. Its typed cutscene owner is
called 299 times while the source lane reports zero typed cutscene calls, so
the comparison proves that the new registration owns the intended entry.
The generated-A32 differential remains the instruction-level reference and
now also compares AAPCS-preserved core/VFP registers, stack, return PC, thread
pointer and FPSCR across all nine cases and restore/replay.

Evidence:

```text
I:\oot3dre_work\native_game\cutscene_update_frame_source_smoke_link_dream.json
SHA-256 e59ca5d4ee2c40ce6a7c482021a69c03c7560632177843f2f02a8eb24a810f75

I:\oot3dre_work\native_game\cutscene_update_frame_a32_baseline_link_dream.json
SHA-256 10a81e87b63a156251008538d385691aeca7f84cb133390a3f1d66d3a703f322
route: current typed Cutscene_UpdateFrame baseline
```

Formal consumer acceptance is recorded in
`I:\oot3dre_work\source_integration\d5c292708f7f-ca8c22db1095`.
The package remains pinned to producer commit `d5c2927`, reports all eight
source graphs closed, four runtime-ready owner records and no remaining
closure worklist. Producer source does not need reopening for this owner.
`Cutscene_ProcessCommands` remains an independent consumer migration target.

## 2026-07-29 Cutscene_ProcessCommands runtime closeout

The independent `Cutscene_ProcessCommands` migration is complete as an
explicit Native30 opt-in. The consumer imports the full body at
`0x002C5BA0-0x002C82D3` from producer commit
`d5c292708f7f25f921a066449db3cea43fad73c4`; all command records, literal
cells and direct dependencies continue to use target addresses and OOT3D
guest memory.

Process acceptance exposed three source/ABI details that the initial
single-command fixtures could not prove:

1. `default_oot3d_dup3:` is the generic outer command handler, but the
   maintained C expresses it as a plain Ghidra label. The reproducible
   consumer lowering restores the missing C++ `default:`. Without it,
   command `0x0D` advances only four bytes and interprets its count as the
   next command.
2. The first parameter of `FUN_0033cb90` is input camera-track state.
   Target code reads its first two words and writes directly through the
   third argument. Guest scratch is therefore `CopyIn=true`,
   `CopyOut=false`; the former output-only contract supplied uninitialized
   pointers to the A32 dependency.
3. `FUN_003665fc` has three core-register parameters even though two owner
   call sites in the maintained C omit the third. Direct disassembly of the
   pinned `code.bin` proves `(0xE, 1, 1)` at `0x002C61C0` and
   `(0xF, 0, 0)` at `0x002C61E8`. The missing `r2` value changed
   `0x153001A4`, then reached DSP state on the following refresh. The
   reproducible consumer lowering now makes both arguments explicit.

The differential now includes a two-command generic-stride case and an
environment-flag ABI case in addition to the empty, service, camera-scratch
and dynamic-callback paths. It compares the source owner with generated A32
and passes all six scenarios. The generated owner is byte-reproducible from
the pinned maintained source.

Runtime activation is:

```text
--enable-source-cutscene-process-commands
```

It is accepted in both Native30 presentation modes. The deterministic
process differential uses `native30_no_interpolation`; visual interpolation
does not alter guest updates. Enhanced60 rejects the switch and retains its
separate typed frame-crossing path, so source-owner acceptance does not alter
unlocked gameplay timing.

Final process evidence:

```text
I:\oot3dre_work\native_game\cutscene_process_commands_a32_baseline_link_dream_accepted.json
SHA-256 9004795e6a67e602de973736aaba89b83a4d74cf47abfb9c7a08168df40e4372

I:\oot3dre_work\native_game\cutscene_process_commands_source_smoke_link_dream_accepted.json
SHA-256 ddfeb034745612f83e3d22bafa33d75efcb44896b3dbce46313ba2a5b50419d9
```

The fixed-delta Link-dream pair completes 30 presentations at guest refresh
2,939 and tick `13133012413`. A32 and source produce the same guest-memory
content fingerprint (`13455304922352739916`), DSP PCM fingerprint
(`8058772573127251443`), 201 DSP frames, 29 actor-owner calls and 377 actor
callbacks. The source path executes 29 `Cutscene_UpdateFrame` calls, 29
`Cutscene_ProcessCommands` calls, 295 measured dependency calls, 7,195 guest
reads, 120 guest writes, 27 scratch calls, eight hard-float calls, six
conversion operations and zero failures. `memory_state_fingerprint` and
`process_state_fingerprint` intentionally differ because they include host
write-generation and implementation-operation counts, not guest-observable
content.

Consumer runtime status is now 5/8 accepted:
`GameState_Update`, `Actor_UpdateAll`, `Actor_InitContext`,
`Cutscene_UpdateFrame` and `Cutscene_ProcessCommands`. Producer closure
remains 8/8; future source refinement should encode the three contracts
above without changing their target behavior.

## 2026-07-30 Camera_Update runtime closeout

The consumer has accepted `Camera_Update@0x002D84C4` as an explicit
Native30-only source owner. The complete 4,040-byte body is generated
reproducibly from producer commit
`d5c292708f7f25f921a066449db3cea43fad73c4`; its target body SHA-256 is
`9064f6a8d3677fea176a2a1c07cf0ecb5560e6ebd98ca00e99b422acc8c60981`.

Two target contracts should be incorporated into future producer source:

1. The call to `FUN_002d064c` at `0x002D87E8` receives the data index returned
   by `FUN_0047bff8`, not the floor-polygon pointer. Target instructions
   `0x002D87CC-0x002D87E8` save `r0` in `sl` and move `sl` to `r1`.
   The maintained source currently passes `floorPoly`; in a real checkpoint
   this caused a memory fault at `0x002D0698`.
2. `Camera_Update` arithmetic observes FPSCR `RMode`. The retained checkpoint
   uses round-toward-zero and differs by one ULP from host round-to-nearest in
   the floor-normal conversion. Producer types should not imply that ordinary
   host floating-point semantics are sufficient at this boundary.

The consumer generator asserts the exact stale call expression before
replacing it, so a producer refinement will fail loudly rather than applying
the correction twice. The floor-camera differential forces both dependency
calls after the raycast and records `r0-r2`; the other four scenarios cover
hidden sret, early return, normal camera and dynamic camera dispatch.

Final process reports:

```text
I:\oot3dre_work\native_game\camera_update_a32_baseline_link_dream_accepted.json
SHA-256 7da50afaed77e8c4c54012c9742b04332fc3c1f5d10661f88dcb91a605c2dbbd

I:\oot3dre_work\native_game\camera_update_source_link_dream_accepted.json
SHA-256 4c26d89c47b3dca8952f70976c7b216eddc9d56861c50533791ba373f53994e3
```

At refresh 2,939, both paths have identical guest-memory content, system tick
and PCM. The source owner records 58 calls, 632 nested dependencies, 2,030
guest reads, 1,044 guest writes, 261 scratch calls, 232 hard-float calls, 29
dynamic callbacks and zero failures. Host write-generation fingerprints are
not equivalence keys.

Consumer runtime acceptance is now 6/8:
`GameState_Update`, `Actor_UpdateAll`, `Actor_InitContext`,
`Cutscene_UpdateFrame`, `Cutscene_ProcessCommands` and `Camera_Update`.
The remaining source-owner work is the Player pair; the producer's eight
closure graphs remain closed.

## 2026-07-30 Player_Update runtime closeout

The consumer has accepted `Player_Update@0x001E1B54` as an explicit
Native30-only source owner. The complete 628-byte target body is generated
reproducibly from producer commit
`d5c292708f7f25f921a066449db3cea43fad73c4`; its target SHA-256 is
`7c0fc5405ce9576c5b4fe379c4fb0e009b148ad800b0663e58f16d47927dd12e`.

Three target contracts should remain explicit in future producer source:

1. Instruction `0x001E1B90` is `STRH` under condition `LT`. The unavailable
   object path writes two bytes to the global field, not a four-byte word.
2. The owner uses target binary32 operations and `vcvt.s32.f32`; host
   arithmetic and casts are insufficient for NaN, exception-flag and
   rounding equivalence.
3. The owner frame is exactly `0x60` bytes below caller `SP`, and its
   48-byte `Input` local starts at owner `SP+0x10`. This stack identity is
   observable by `Player_UpdateCommon` and by the hard-float `Actor_Spawn`
   stack arguments.

The consumer generator pins the maintained source hash and asserts its
critical source fragments. Its differential executes six branch-complete
scenarios and compares full mapped RAM, dependency ABI, preserved
registers/VFP state and FPSCR with generated A32.

Final process reports:

```text
I:\oot3dre_work\native_game\player_update_a32_baseline_link_dream_accepted.json
SHA-256 6cb820acbf2ab58f1d4fe64a22d2a36cf85a05ac1f593d61925c48aeb386c208

I:\oot3dre_work\native_game\player_update_source_link_dream_accepted.json
SHA-256 df52b3030e99fea98654ff4293895d0a10251bdb17995de750d47bfe97e5dc61
```

At refresh 2,939, A32 and source have identical guest-memory content, system
tick, temporal-event ledger and PCM. The source owner records 29 calls, 29
`Player_UpdateCommon` calls, 261 guest reads, 203 guest writes, 87 target VFP
operations and zero failures. Host write-generation and dispatcher
fingerprints are not equivalence keys.

Consumer runtime acceptance is now 7/8. Producer closure remains 8/8 and
does not need another closure package. The sole remaining consumer migration
is the independently owned `Player_UpdateCommon@0x00250AD0`.

## 2026-07-30 Player_UpdateCommon runtime closeout

The consumer has accepted the final vertical owner,
`Player_UpdateCommon@0x00250AD0`, as an explicit Native30 source path:

```text
--enable-source-player-update-common
```

The revision-pinned import retains the complete
`0x00250AD0-0x0025342C` body (10,588 bytes, target SHA-256
`b2a9244e5eb77976fae277b6d16e01f2ea6d6263d525b8c6fb9b09d1d9307d7e`),
136 target literal cells, 88 direct target-address dependencies and the
dynamic Player action callback. The exact `0xA0`-byte call frame, checked
guest accesses and FPSCR-sensitive operations remain observable to nested
ARM dependencies.

Two type contracts should remain explicit in future producer source:

1. `DAT_00251304` is a `short*`, not an integer address. Target disassembly
   proves the scaling: maintained-source offsets `+0xAC`, `+0x14`, `+0x1C`
   and `+0x82` become ARM byte offsets `+0x158`, `+0x28`, `+0x38` and
   `+0x104`. The consumer generator asserts the stale `uintptr_t` declaration
   before applying this revision-pinned correction.
2. The input test is a real double dereference,
   `*(u32*)(*(u32*)(Player+0x29C8))`. A same-type C++ proxy copy initially
   collapsed the inner read and falsely tested bit `0x200` in the pointer
   value. `GuestIndirectRef<T>` now loads the target pointer explicitly, and
   its regression test covers both read and write-through behavior.

The second issue was found by broadening acceptance beyond Link-dream. At
the ladder checkpoint it produced one differing guest byte at
`Player+0x1700`; ARM block tracing proved the target took
`0x00250CB0 -> 0x00250CE0 -> 0x00250CF0`, while the faulty lowering selected
the timer-8 branch. After correcting the general indirect-access lowering,
the complete memory content is exact.

Retained evidence:

```text
I:\oot3dre_work\native_game\player_update_common_a32_baseline_link_dream_accepted.json
SHA-256 3e3e559ba1d0199674d8cbf0a12e8b3c08bf85df6ae46ee84f77cfdfede08042

I:\oot3dre_work\native_game\player_update_common_source_link_dream_accepted.json
SHA-256 491875d10f73827766f3c454051650881e3ea12ed19e0fff94a6b138f6f005d4

I:\oot3dre_work\native_game\player_update_common_a32_gameplay_accepted.json
SHA-256 72bea70337622b364d47dbf6c28cb47c27b76de1cdb40a29fb4cbaaafe7078b

I:\oot3dre_work\native_game\player_update_common_source_gameplay_accepted.json
SHA-256 2c367ca472b2150df398eed68e414f8db2bbaabefa95d20ccd058341e32d452e
```

Link-dream is exact for 60 presentation frames and 30 owner calls, including
guest-memory content, Player state, system tick, PICA work and DSP PCM. The
gameplay pair is exact for 180 presentation frames, 90 owner calls, 4,314
Actor callbacks, memory fingerprint `10256699531768752404` and PCM
fingerprint `15922785005745006140`. Both source runs report zero failures.
Host write-generation and dispatcher-operation fingerprints remain
implementation metrics rather than equivalence keys.

Consumer runtime acceptance is now 8/8. All producer closure graphs and all
eight selected vertical runtime owners are closed; no additional producer
package is required for this milestone.

## 2026-07-30 composed consumer profile

The consumer now composes all accepted roots with:

```text
--enable-source-gameplay-profile
```

This is a Native30 selection profile, not a new producer owner. It selects
the seven independent imported source bridges and requires the typed
`GameState_Update` as the eighth root. Enhanced60 and configurations that
disable compiled or typed gameplay are rejected. Individual owner switches
remain available and unchanged.

The profile has exact fixed-delta equivalence with the corresponding seven
explicit switches for 60 Link-dream presentation frames: full guest memory,
process state, tick schedule, framebuffer state, 2,530 draws, 180 PICA
completions, all owner counters and PCM fingerprint
`4924382187376014073` match. The retained report is:

```text
I:\oot3dre_work\native_game\source_gameplay_profile_link_dream_accepted.json
SHA-256 ce16eabbd911aa4a62a8c2d74a5af9c2d539bb40f9a8d9fbbc63f548ab55a5e5
```

No producer action is requested for this composition step. Remaining A32
execution can now be ranked from the composed profile as consumer hotspot
work while preserving the revision-pinned owner imports.

## 2026-08-06 CSAB curve evaluator source-overlay promotion

The consumer imported and promoted the native CSAB S16 and F32 curve
evaluators at `0x003084E8` and `0x003087A4` into the hot source overlay. The
whole-AOT path remains the fallback for unpromoted owners. One maintained
source mismatch is handled inside the pinned consumer import:

```text
z_skel_anime_csab.c: Oot3d_CsabTangentFromS16
current: sinf(angle)
target:  `Math_TanF@0x003555D8`, including its native range reduction,
         classifier, VFP polynomial and final `CPSR` ownership
```

Target callsites `0x003086FC` and `0x0030871C` both branch to the helper. The
shared source core reproduces the helper and its nested stack writes without
using host libm. A live bounded probe reported `1003/1003` handled calls,
zero fallbacks and zero differential mismatches against whole-AOT. Complete
evidence and hashes are in
`tools/oot3d/source_imports/csab_curve_eval_d5c2927/import_manifest.json` and
`docs/OOT3D_CSAB_SOURCE_OVERLAY_OWNER.md`.

No additional producer action is requested for this owner. Consumer work can
move to the next complete visible subsystem while preserving the same
source-overlay/whole-AOT fallback contract.
