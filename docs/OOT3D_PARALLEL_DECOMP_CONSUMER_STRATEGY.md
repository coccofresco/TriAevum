# OOT3D parallel decomp consumer strategy

## Objective

Turn semantic progress from `I:\oot3decomp` into reviewable source-owner
integration units without modifying that repository, coupling it to the normal
runtime build, or importing bulk recovered C as if it were finished source.

The producer and consumer can therefore advance independently:

```text
I:\oot3decomp
    committed semantic C/C++ + generated analysis evidence
        |
        v
revision-pinned owner closure generator
        |
        v
I:\oot3dre_work\source_integration\<immutable snapshot>
        |
        v
explicitly approved owner import into the native runtime
```

OOT3D target evidence remains authoritative. Existing A32 execution and
Native30 behavior provide the differential baseline while an owner is being
replaced. N64 source is not imported into these units and does not define
OOT3D layout, timing or control flow.

## Implemented components

| Component | Path | Responsibility |
| --- | --- | --- |
| Owner contract | `tools/oot3d/source_integration/owner_roots.json` | Declares gameplay roots, priorities, service boundaries, profiles and explicit blockers. |
| Consumer semantic overlays | `tools/oot3d/source_integration/consumer_semantic_overlays.json` | Records independently reconstructed target-linked bodies and their decomp handoff. |
| Closure generator | `tools/oot3d/source_integration/build_owner_closure.py` | Builds direct transitive closures, classifies source maturity, resolves indirect evidence and ranks missing work. |
| Regression tests | `tools/oot3d/source_integration/test_build_owner_closure.py` | Verifies commit pinning, indirect blockers, hot-profile ranking, immutable reruns and consumer-input identity. |
| Operator entry point | `scripts/oot3d/Invoke-Oot3dDecompOwnerSync.ps1` | Runs tests and creates a snapshot without writing below the decomp root. |
| Generated exchange | `I:\oot3dre_work\source_integration` | Stores immutable packages and a `latest.json` pointer outside both repositories. |
| Decomp handoff guide | `docs/OOT3D_CONSUMER_TO_DECOMP_HANDOFF.md` | Defines how producer work absorbs and supersedes consumer-reviewed semantics. |

The normal game build has no live dependency on `I:\oot3decomp`. This is
intentional: an in-progress decomp edit cannot invalidate or lengthen an
ordinary renderer/runtime build.

## Input contract

Tracked decomp inputs are read with `git archive` from one exact commit. Local
tracked edits are counted and ignored. The generated callgraph and
function-pointer reference sidecars are read once and recorded by SHA-256
because they are intentionally not tracked by the producer. The generator
records the checkout revision that supplied those sidecars, verifies that
neither the checkout nor either sidecar changes during analysis, and reports
whether that revision matches the pinned source commit. A historical source
snapshot therefore never silently claims that newer generated evidence came
from the pinned commit; exact topology reproduction requires regenerating the
sidecars from an independent checkout of that revision.

Snapshot identity covers:

- the exact decomp commit;
- generator version and owner configuration;
- callgraph and function-pointer evidence;
- every hot-source profile;
- every native runtime entry registry consumed by the analysis.

This prevents two materially different analyses from sharing a package name.
Running the same inputs again is idempotent and only refreshes `latest.json`.
When several packages exist for the same producer revision, delta generation
prefers the nearest ancestor with identical complete inputs, then the same
stable owner/configuration/profile/runtime-registry contract. This prevents a
preliminary package from becoming the baseline merely because its directory
name sorts first.

`source_status_overrides` is the consumer-side quarantine list. It can demote
a producer promotion when direct target evidence proves an ABI or semantic
mismatch. A quarantined entry remains visible in the work request, cannot
close an owner and cannot overwrite an already correct runtime implementation.

The producer lane `semantic_certified_bulk_c` is represented separately as
`semantic_certified_bulk`. These bodies have target identity, mapped
dependencies and compile-qualified C, but still require local type and
expression cleanup. They receive a dedicated work queue and never satisfy an
owner closure until promoted to reviewed semantic source.

Dynamic target calls that cannot appear as ordinary callgraph edges are
separate owner contracts. A contract records the target-proven field layout,
dispatch ABI, intended typed resolver and transitional A32 fallback. An open
contract blocks source closure. It can become `implemented` only with a named
runtime implementation and differential evidence, so promoting individual
callbacks cannot accidentally make a dispatcher owner appear complete.

The consumer can also close a missing body independently through
`consumer_reviewed_semantic`. Such an overlay hashes its implementation and
tests, records exact target evidence and emits a producer-facing semantic
handoff. It remains visibly distinct from producer `reviewed_semantic`; once
the producer publishes the entry, the next delta reports the overlay as
superseded.

## Initial owner set

The first closure frontier is deliberately owner-oriented rather than a list
of isolated leaf functions:

1. `Player_Update` and the candidate `Player_UpdateCommon`;
2. `Actor_UpdateAll`;
3. cutscene frame update and `Cutscene_ProcessCommands`;
4. `Camera_Update`;
5. `GameState_Update`;
6. the game-state renderer/environment initializer.

These roots cover the highest-value path away from whole-A32 gameplay:
player behavior, actor dispatch, cutscene control, camera, scene state and
environment submission. More owners should be added only when they form a
separately testable behavioral boundary.

## Closure and import gates

An owner is a source candidate only when:

- every reachable direct dependency is reviewed semantic source;
- exact anchors and bulk-recovered bodies have been promoted or bounded;
- no reachable source promotion is quarantined by target evidence;
- every reachable function-pointer edge has a reviewed target or explicit
  host contract;
- every reachable dynamic callback or virtual dispatch has an implemented,
  verified owner contract;
- all manually recorded identity or Ghidra-boundary blockers are resolved.

Closure only materializes a candidate source tree. Runtime import remains an
explicit second decision through `approved_owners`. Every listed owner must
have a matching, content-addressed `owner_import_acceptance` record and
requires:

1. ABI and layout assertions at the boundary;
2. Native30 differential tests against the existing execution path;
3. savestate round trips before and after the owner;
4. callback and indirect-target coverage;
5. timing classification for Enhanced60;
6. performance evidence showing that the owner actually removes A32 work.

This keeps decomp completeness, runtime correctness and 60 FPS semantics as
separate measurable properties.

## Parallel operating loop

Run a sync whenever the decomp publishes a meaningful commit:

```powershell
scripts\oot3d\Invoke-Oot3dDecompOwnerSync.ps1 -RunTests
```

Pin a historical producer revision when reproducing a result:

```powershell
scripts\oot3d\Invoke-Oot3dDecompOwnerSync.ps1 `
  -Revision 08504051b6ec504d383b2ada2928f6d23673bdc5
```

The resulting package provides three complementary queues:

- `decomp_work_request.json`: top missing semantic entries and manual
  structural questions, including target-backed quarantines and the ranked
  semantic-certified cleanup queue, for the decomp workstream;
- `consumer_work_request.json`: dynamic dispatch boundaries and owner import
  gates that must be implemented in this runtime rather than pushed back onto
  the decomp workstream;
- `decomp_handoff.json` and `decomp_handoff.md`: consumer-reviewed semantic
  bodies, exact evidence and suggested producer integration;
- `owner_worklist.csv`: full ranked consumer frontier, weighted by owner
  depth, fan-in and measured Kokiri/title hot profiles.

After the next producer commit, `delta.json` shows whether each owner moved
toward closure. This avoids repeatedly re-reading the entire decomp or
claiming progress from unrelated function-count growth.

## Package contents

```text
<snapshot>/
  manifest.json
  report.md
  delta.json
  decomp_work_request.json
  decomp_handoff.json
  decomp_handoff.md
  consumer_work_request.json
  source_entry_registry.csv
  owner_worklist.csv
  owners/<owner>/
    closure.json
    missing_entries.csv
    candidate_source_files.txt
    source_candidate/          # present only after full static closure
```

`manifest.json` records all source and artifact hashes. `report.md` is the
human summary. The CSV/JSON files are the stable interface for scripts and
future CI.

## Efficient implementation order

For each sync:

1. Resolve manual identity and function-boundary blockers first.
2. Clean semantic-certified bodies already intersecting an owner closure.
3. Promote direct owner callees before deep leaves.
4. Prefer missing functions shared by multiple owners.
5. Within equivalent graph positions, prefer measured hot functions.
6. Implement and verify dynamic callback boundaries before importing an owner.
7. Close remaining statically identifiable indirect callback sets.
8. Import one complete owner behind the existing typed bridge.
9. Remove the corresponding A32 island only after differential acceptance.

The certified-cleanup queue implements step 2; the general priority score
encodes steps 3 through 5. Both are guidance rather than semantic authority;
target evidence is still required for every promotion.

## Historical 294123b owner-closure checkpoint

The current exact exchange package is:

```text
I:\oot3dre_work\source_integration\294123b03806-035e6576800b
```

It uses source and regenerated sidecars from
`294123b03806643005cc8981a8179a8a52236564`, retained read-only at
`I:\oot3dre_work\decomp_snapshots\294123b03806`. Both sidecars identify that
same commit. The moving `I:\oot3decomp` checkout is not package evidence.

Relative to the former `d1f8b1d9` analysis, the producer adds 71 reviewed
target-linked bodies. Fifteen intersect configured owners and reduce the
ranked worklist from 927 to 912. The 1,050 architecture-certified bulk bodies
do not represent the same progress: only 18 intersect owners and none closes
a source node until reviewed semantic promotion.

The producer should now work in this order:

1. Verify the ARM EABI/compiler-runtime family rooted at `0x00332754`.
   `decomp_work_request.json` records exact spans, hashes and all affected
   owner depths. If confirmed, fix the Ghidra boundary at `0x002DF174`, expose
   exact ABI-correct service symbols and regenerate both sidecars.
2. Absorb the nine target-differential consumer overlays, including
   `Actor_Destroy` and the accepted `GameState_Update`; semantic bulk
   certification does not supersede either overlay.
3. Graduate owner-local certified candidates: Actor first, then the shared
   cutscene bodies and `LightContext_InsertLight`.
4. Close `actor_update_all` vertically. It is the smallest blocked gameplay
   owner, with 107 unresolved static bodies, no indirect edge and no dynamic
   contract. Once the compiler family is resolved, its next bounded subtree
   is direct child `0x00477E44`: eight nodes, five unresolved.
5. Continue with the renderer initializer, the two shared cutscene owners,
   Camera and finally the two large Player owners. Shared leaves may be
   promoted earlier when they reduce several closures at once.

The 41 passing producer tests at this checkpoint establish metadata and policy
coherence, not behavioral equivalence. Owner closure additionally requires
target/ABI tests, no bulk-only or certified-only reachable body, no open
indirect/dynamic/manual contract and sidecars regenerated from the committed
source revision. Runtime activation remains a separate hashed acceptance.
`docs/OOT3D_CONSUMER_TO_DECOMP_HANDOFF.md` is the detailed producer procedure.

## Guardrails

- Never edit, clean, switch or commit `I:\oot3decomp` from this consumer.
- Never consume uncommitted tracked source from the producer.
- Never auto-link a newly closed owner into the game.
- Never approve an owner without hashed implementation and verification
  evidence.
- Never accept a producer maturity label over contradictory target evidence.
- Never treat a runtime typed leaf as proof that its owning graph is native.
- Never use total decompiled-function count as the migration metric.
- Never make the standard build scan the live decomp checkout.
- Keep Native30 available as the behavioral oracle until an owner passes.

## First closed owner

`GameState_Update` at `0x00417014` is the first closed and accepted owner. Its
one-node static graph is supplied by a consumer-reviewed semantic overlay; the
generic `GameState.main` dynamic contract is implemented through the normal
typed-or-A32 dispatcher. Differential tests cover the exact A32 entry and
continuation, and a process-state round trip covers restoration at the
callback boundary.

The implementation is isolated in
`tools/oot3d/native_game_runtime/oot3d_typed_game_state.*`. Its acceptance
claims only removal of the owner entry and continuation A32 blocks; callback
bodies remain independently migratable. The generated handoff gives the
decomp enough target evidence, type offsets and tests to publish its own
portable source and supersede the overlay.

## b49f291 alignment

The pinned consumer package for decomp commit
`b49f2916989082eb0dcf6be9f0d8ed80ddf3a181` is generated below
`I:\oot3dre_work\source_integration`. It records 26 producer promotions relative
to `0850405`.

Two promoted functions are deliberately quarantined:

- `0x003404A8`;
- `0x00358DFC`.

Their `b49f291` C source fixes animation speed to `1.0f`, while the target
instructions preserve incoming hard-float argument `s0` in `s16` and restore
it before tail-calling `0x00360190`. The existing typed bridge preserves this
argument and therefore remains authoritative until the producer source is
corrected. Of the remaining promotions, `0x003723C0` is the only new reviewed
helper directly shared by the configured Player and Actor owner closures; its
large collision implementation dependency is not yet a typed runtime boundary.

## bbeade9 alignment

The intermediate producer checkpoint
`bbeade9c5b91bd699a6863bc9cae2dd596554ea6`, relative to the final `b49f291`
consumer package, adds 86 reviewed semantic bodies and introduces 84
semantic-certified bulk bodies.

The configured closure frontier advances without closing an owner:

- Player and the Player common candidate each reduce their missing set by 8;
- Camera reduces its missing set by 3;
- both cutscene owners reduce their missing set by 2;
- Actor update reduces its missing set by 1;
- GameState and renderer/environment initialization are unchanged.

No source is linked into the runtime from this checkpoint. The two hard-float
animation wrappers remain quarantined because their producer implementation
still discards incoming `s0`. The next productive producer work is therefore
the certified-cleanup queue and the direct/manual blockers emitted in
`decomp_work_request.json`, not an unsafe partial owner import.

## 80747d7 alignment

The pinned producer checkpoint was
`80747d70a8bc5d670974e5397c38ec986d3d59c0`. Its generated sidecars come from
the same commit; later tracked worktree edits were observed and ignored.
Relative to `bbeade9`, 16 certified bodies graduate to reviewed semantic
source, leaving 68 in the cleanup lane and producing no regressions.

The immediate owner effect is deliberately modest:

- Player and the Player common candidate each reduce their missing set by 1;
- both cutscene owners reduce their missing set by 1;
- Actor, Camera, GameState and renderer/environment initialization do not
  change;
- no owner closes and no runtime import is enabled.

The immutable package is generated under
`I:\oot3dre_work\source_integration\80747d70a8bc-e2aed2bb7576`.

## faf4b2c alignment

The pinned producer checkpoint was
`faf4b2c767afef82cb7912aef150ef317a6f7c68`. The immutable consumer package is
`I:\oot3dre_work\source_integration\faf4b2c767af-f3c17bbd5cbb`. It reads only
committed producer files; later tracked edits in the decomp worktree are
reported and ignored.

Relative to `80747d7`, ten semantic-certified actor bodies graduate to reviewed
source and 58 certified bodies remain. All ten promotions are outside the
configured direct owner closures, so none of the eight owner missing counts
changes. This is expected for actor callback bodies: the static callgraph
cannot represent calls loaded from each actor instance.

The consumer now records three open contracts on `actor_update_all`:

- actor `init` and `update` callback dispatch at `0x00461344`;
- actor `destroy` callback dispatch through `Actor_Destroy` at `0x002D644C`;
- virtual cleanup of the model context and owned renderer resources in
  `Actor_Destroy`.

The field offsets and callsites come from OOT3D target evidence and the
committed typed actor layouts. The intended runtime boundary first resolves a
reviewed typed entry, then uses the existing A32 call path for an unmigrated
callback. No contract is marked implemented yet, and no runtime import is
enabled. This makes the next consumer task explicit without turning the ten
new leaf promotions into a misleading owner-closure claim.

## 5379fda consumer closure

The source package remains pinned to producer checkpoint
`5379fda60cf06c84e7af65061018a0ecc6c603f8`. Its current immutable package is
`I:\oot3dre_work\source_integration\5379fda60cf0-92e72b0e5971`.

Relative to `faf4b2c`, the producer side still contributes 20 certified-body
promotions and leaves 38 bodies in the target-wide cleanup lane. The consumer
now independently closes and accepts `game_state_update`:

- closure size: one function;
- producer reviewed: zero;
- consumer reviewed: one;
- open indirect, dynamic and manual blockers: zero;
- runtime import ready: yes;
- ranked worklist: 945 entries.

The source commit is historical while the generated sidecars were observed at
producer checkout `dd4892c538e5de02675d29f3b8132c4d8567d713`; the package
records the mismatch and does not claim exact `5379fda` topology. This does not
weaken the one-node `GameState_Update` closure: its body and dynamic callback
contract are verified directly from the exact target instructions. Exact
historical analysis of larger owners still requires sidecars regenerated at
the pinned source commit.

The current producer still carries `GameState_Update` as bulk-recovered source.
`decomp_handoff.md` and `decomp_handoff.json` therefore mark it
`ready_for_producer_review`. The remaining actor callback contracts stay open
and are the next consumer-owned structural work.

## 2974715 actor lifecycle closure

The consumer evidence checkpoint is
`297471509d57f01ae58e08aeaffc45087c2221e4`. The immutable package containing
the consumer work is:

```text
I:\oot3dre_work\source_integration\297471509d57-95cafee50019
```

The consumer now implements all three dynamic contracts previously recorded
for `actor_update_all`:

- instance init callback at `0x004615D8`;
- instance update callback at `0x004617A4`;
- actor destroy plus model/resource virtual cleanup at
  `0x002D644C`.

The implementation is isolated in
`tools/oot3d/native_game_runtime/oot3d_typed_actor_lifecycle.*`. Callback
continuations live entirely in guest registers, stack and memory. The normal
dispatcher therefore selects an existing typed callback when available and
retains A32 for an unmigrated callback; there is no host callback registry or
non-serializable lifecycle state.

`Actor_Destroy` is also a consumer-reviewed semantic overlay with exact target
hash, ABI, layout, differential coverage and a checkpoint round trip. It is
ready for producer review, not yet producer source. A key correction in the
handoff is its two-argument ABI: `Actor_UpdateAll` supplies `PlayState*` in
`r1`, which remains live into the actor destroy callback even though the
decompiler omitted it from the callee signature.

This closes the actor owner's dynamic-dispatch count from three to zero and
raises the typed entry catalog from 109 to 117. It does not close the owner:
that checkpoint still has 114 static missing bodies and three indirect edges.
The former fused-boundary blocker is resolved from target code: the root ends
with the tail branch at `0x004618F4`, while `0x004796BC-0x00479717` is a
separate dynamic-collision transform helper. Exact ranges, hashes and producer
actions are exported in `decomp_work_request.json` under
`resolved_structural_findings`. The generated report preserves the remaining
blockers explicitly, so this progress cannot be mistaken for a full
`Actor_UpdateAll` migration.

## 2974715 shared record initializer closure

The follow-up exact package is
`I:\oot3dre_work\source_integration\297471509d57-95cafee50019`. Its source,
callgraph and function-pointer sidecars all belong to producer commit
`297471509d57f01ae58e08aeaffc45087c2221e4`; a detached copy at
`I:\oot3dre_work\decomp_snapshots\297471509d57` prevents concurrent producer
commits from changing provenance during generation.

The live producer advanced independently to
`c109c3f43a9a759f1fb23c60590d9b9fe78cab1c` while this package was being
closed. That checkout remains read-only and is not the provenance source for
this result. Import into a later decomp revision must regenerate topology
there first and use this package to reconcile only the still-missing semantic
owner.

The consumer independently promotes `FUN_002ffa20` as a target-verified
one-argument record constructor. It clears exactly `0x50` bytes, returns the
input pointer and is used by `FUN_00350820` for records with stride `0x54`.
The unknown final four-byte field remains explicitly outside the recovered
semantic body.

This single dependency intersects every blocked configured owner. Relative to
the preceding exact package:

- all seven blocked owners lose one missing static body;
- the two indirect references from `0x00418984` and `0x0041F048` resolve;
- `actor_update_all` moves to 58 producer-reviewed, 2 consumer-reviewed,
  113 missing and 1 open indirect edge;
- the typed entry catalog rises from 117 to 119;
- the ranked worklist falls from 939 to 938 entries;
- no additional owner is falsely marked closed or import-ready.

The machine-readable producer request contains the provisional semantic body,
exact target bytes and hash, callback ABI, call sites, verification hashes and
the unresolved type question. The runtime implementation is isolated in
`oot3d_typed_record_initializer.*`; it dispatches the existing native
`Runtime_Memzero` and resumes at the original continuation, so this promotion
does not create a second memory-service implementation.

## d1f8b1d audio callback indirect closure

The exact producer checkpoint is
`d1f8b1d907a7e1b8d823abde58e94e4d900dc5dc`. Its immutable source and
sidecar snapshot is `I:\oot3dre_work\decomp_snapshots\d1f8b1d907a7`; the
generated consumer package is:

```text
I:\oot3dre_work\source_integration\d1f8b1d907a7-a363c5991b6e
```

The live producer subsequently advanced and has local edits, so it remains a
read-only moving input. This result is based only on the exact detached
snapshot above.

The consumer promotes indirect target `0x00465304` as the provisional
`Oot3d_AudioRequestFlag100Callback`. The target:

- is loaded through literal `DAT_00453760` by
  `Oot3d_AudioRequestFlag100`;
- compares referenced-object SFX ID `+0x9C` with request state `+0x28`;
- clears the pending byte at request state `+0x04` for a null handle or an
  unsigned status no greater than 12;
- preserves the original assign, status and clear guest-service calls;
- aliases its saved `r3` stack word with the temporary handle, so final `r3`
  restores the handle rather than the incoming value.

The older `PauseDungeonItem_AssignOptionalObjectRef` label conflicts with this
concrete pointer producer and request-state data flow. It is recorded as a
producer mismatch rather than silently inherited.

Runtime entry and continuation behavior is isolated in
`oot3d_typed_audio_request_callback.*`. Differential tests cover every branch
and the status threshold; a MessagePack checkpoint at the status-return
boundary verifies that no host-only continuation state was introduced.

Relative to the previous exact package:

- the final `actor_update_all` indirect edge closes, from 1 to 0;
- all dynamic and manual blocker counts remain 0;
- static closure remains 173 entries: 58 producer-reviewed, 2
  consumer-reviewed and 113 missing;
- typed entries rise from 119 to 123 because entry and three continuations are
  observable;
- the ranked worklist falls to 932 entries;
- the owner remains correctly blocked and is not source-closed or
  runtime-import-ready.

The next vertical lane contains only static direct dependencies. Selection
must use unresolved descendant count and shared-owner leverage rather than
adding more runtime gates.

## d1f8b1d direct microleaf tranche

The next exact package is:

```text
I:\oot3dre_work\source_integration\d1f8b1d907a7-559a4745a049
```

It uses the same immutable producer and sidecar snapshot as the preceding
audio package. The input change is entirely consumer-owned: three direct
static bodies were reconstructed from target code, implemented as isolated
typed modules and exported as semantic overlays.

The tranche closes:

- `0x00334354` / `Player_ReleaseLockOn`, shared by `actor_update_all`,
  `player_update` and `player_update_common_candidate`;
- `0x0047C938` / `ActorUpdateRecord_InitializeDefaults`;
- `0x0047CCDC` / `ActorUpdateRecord_ClearHalfwords`.

All three are full leaf replacements rather than gates or observational
hooks. Differential coverage includes the exact caller-visible ARM/VFP ABI,
the surrounding guest memory and non-destructive failure. A MessagePack
round trip proves that the implementation adds no host-owned state. The
default-word initializer reads its literal from target memory and is tested
with a nonzero replacement, so native data ownership remains explicit.

Relative to the preceding exact package:

- `actor_update_all` remains a 173-node closure, moves from 2 to 5
  consumer-reviewed nodes and from 113 to 110 missing static bodies;
- both Player owners gain `Player_ReleaseLockOn` as consumer-reviewed source;
- typed entries move from 123 to 126;
- the ranked global worklist moves from 932 to 929;
- indirect, dynamic and manual blocker counts remain zero for
  `actor_update_all`;
- no blocked owner is mislabeled source-closed or runtime-import-ready.

The next Actor selection should remain vertical. Prefer a direct child whose
descendants are already reviewed, then a small direct subtree, before taking
the large shared renderer initializer. This steadily shrinks the root's
static frontier while keeping each commit independently differential-testable
and directly transferable to the producer.

## d1f8b1d Pause UI alpha subtree

The next exact package is:

```text
I:\oot3dre_work\source_integration\d1f8b1d907a7-e13f3a2762ef
```

The consumer closes direct child `0x0047955C` as
`PauseUi_UpdateDualAlpha`. Its complete closure contains only the root plus
producer-reviewed `PauseContext_GetState` and `Math_StepToS`; no descendant
remains unresolved.

This is a full owner-body promotion, not a timing gate. The typed module:

- preserves the target `r1` state-pointer ABI and 24-byte ARM/VFP frame;
- reads the update pointer, 0.5 bias and four phase factors from native guest
  literals;
- retains the pause query and both signed-step operations as guest services;
- exposes entry and three continuations to whole-AOT;
- stores no continuation state on the host.

Differential tests execute generated A32 for the owner and both services at
every boundary. A MessagePack checkpoint between the first and second
`Math_StepToS` proves process-state portability.

Relative to the preceding exact package:

- `actor_update_all` remains a 173-node closure, moves from 5 to 6
  consumer-reviewed nodes and from 110 to 109 missing static bodies;
- typed entries move from 126 to 130;
- the ranked global worklist moves from 929 to 928;
- indirect, dynamic and manual blocker counts remain zero;
- the owner remains correctly blocked and not runtime-import-ready.

Continue vertically from another direct Actor child or its smallest unresolved
subtree. Avoid broad shared renderer roots until their descendants provide
better closure leverage than an independently testable gameplay subtree.

## d1f8b1d Dyna interaction leaf

The next exact package is:

```text
I:\oot3dre_work\source_integration\d1f8b1d907a7-9dbe8f7cf190
```

The consumer closes direct Actor leaf `0x0047AF24` as the provisional
`DynaPoly_ResetActorInteractFlagsIfRegistered`. The implementation scans the
two target-native Dyna registry views, validates the `0x6C` actor record and
clears only byte `+0x1B8` on the matching Actor.

This node required preserving a non-obvious structural fact: Ghidra attaches
the conditional tail at `0x00483C9C` to the function even though the main body
ends at `0x0047AFB0`. Main body, tail and concatenated hashes are recorded
separately in the semantic overlay and generated handoff.

Differential coverage compares empty, inactive, deleted, nonmatching and
first/middle/final matching registries against generated A32. It includes all
ARM/VFP state, both native epilogues, guest stack and surrounding memory.
MessagePack restore is tested before execution, and failure fallback is
non-destructive.

Relative to the preceding exact package:

- `actor_update_all` remains a 173-node closure, moves from 6 to 7
  consumer-reviewed nodes and from 109 to 108 missing static bodies;
- typed entries move from 130 to 131;
- the ranked global worklist moves from 928 to 927;
- indirect, dynamic and manual blocker counts remain zero;
- the owner remains correctly blocked and not runtime-import-ready.

The next selection should again compare the remaining direct Actor children
by unresolved descendant count. Keep large shared renderer roots deferred
unless they become the smallest complete vertical closure.

## d108e7a semantic wave and Actor closure

The current exact package is:

```text
I:\oot3dre_work\source_integration\d108e7a0f2a8-4a77de5d1200
```

Counting only closed owners understates this tranche. Relative to the
historical `294123b` checkpoint, the producer adds 724 reviewed target-linked
bodies. The eight configured graphs now have 3,160 of 3,689 resolved
memberships (85.66%). Deduplicating shared functions gives 1,016 of 1,221
resolved entries (83.21%), with only 205 unique entries still open. These two
coverage measures are generated into every package and into
`consumer_work_request.json`; future documentation-only producer commits no
longer hide the accumulated semantic gain behind a zero immediate delta.

`actor_update_all` is the second source-closed owner and the first substantial
one:

- 171 closure entries;
- 170 reviewed semantic functions;
- one compiler-service boundary;
- no static, indirect, dynamic or manual blocker.

Source closure and runtime acceptance remain separate. The existing consumer
implements selected lifecycle entries and callback continuations, but
`Oot3dTypedGameplayEntryPoints()` does not contain the complete root
`0x00461344`. The package therefore materializes the 32 logical source inputs
and support headers as a 139-file immutable candidate, then emits:

```text
owners\actor_update_all\runtime_import_work_order.json
```

The work order records closure and source-tree hashes, current boundary
evidence, required differential/savestate checks and the prohibition on both
automatic activation and live decomp checkout dependencies. Actor remains on
the A32 path until that order produces a hashed acceptance.

The next producer closure should be
`game_state_renderer_initializer` (18 static entries, no indirect target).
Then close the shared cutscene pair (58/59 missing), Camera (33 static plus 24
indirect), and finally the Player pair (180/181 static plus 55 indirect each).
The consumer can advance independently by implementing the isolated Actor
guest-memory/callback adapter against the pinned candidate.

## e2e28e2 register-ABI wave

The current exact package is:

```text
I:\oot3dre_work\source_integration\e2e28e2d4cf4-4a77de5d1200
```

The producer promotes 101 target-linked identities in one auditable
iteration. Fifty-nine intersect configured owner closures and 42 improve the
target-wide reviewed registry outside them. The direct owner delta is 143
memberships:

- Player and Player-common gain 44 each and resolve one indirect target each;
- cutscene-frame ownership gains 17;
- cutscene-command processing gains 16;
- Camera gains 14 and resolves one indirect target; and
- renderer initialization gains eight.

Weighted semantic owner coverage reaches 3,508/3,689 (95.0935%), up 3.8764
percentage points from the preceding snapshot. Unique closure coverage reaches
1,147/1,221 (93.9394%), up 59 identities and 4.8321 points. The worklist falls
from 139 to 79 entries.

Renderer initialization is the next vertical closeout at 175/177. Its only
missing entries are root `0x0044E7C0` and shared dependency `0x00302BB8`;
neither has an indirect blocker. Once these two land, retain the remaining 74
unique open closure identities as one macro-tranche rather than returning to
small batches. Its ranked front should start with the four
semantic-certified bodies, owner roots, shared hot/runtime leaves and the
three remaining cross-owner indirect-target families.

Source closure and runtime acceptance remain separate. Actor and GameState
are the only source-closed owners; GameState is still the only
runtime-import-ready owner. The consumer can continue Actor acceptance in
parallel while the producer closes Renderer and the cross-owner residual.

## d3817bc Renderer source-closeout wave

The current immutable package is:

```text
I:\oot3dre_work\source_integration\d3817bc96da3-4a77de5d1200
```

The producer processes 101 complete target bodies and records 99 new reviewed
identities. Eleven intersect configured owner closures; 88 improve the
target-wide registry outside them. The direct semantic owner delta is 28
memberships:

- Player and Player-common gain nine each;
- both cutscene owners gain four each; and
- renderer initialization gains its final two.

Weighted owner closure reaches 3,536/3,689 (95.8525%), up 0.7590 percentage
points. Unique closure reaches 1,158/1,221 (94.8403%), up 11 identities and
0.9009 points. The worklist falls from 79 to 67 entries: 63 direct-closure
identities and four indirect targets.

Renderer initialization is now the third source-closed owner at 177/177,
after GameState and Actor. This does not activate it. The consumer should
materialize the pinned 177-function candidate, implement the guest-memory and
callback boundary, retain generic A32 fallback, and require Native30,
savestate and owner-level differential evidence before adding a hashed
acceptance.

Producer work should retain the 67-entry residual as one aggregate frontier.
Its priority order is the owner roots, four certified bodies, shared
hot/runtime dependencies, the four indirect targets and then isolated cold
leaves. Current effective closure is Player 1,032/1,090, Player-common
1,032/1,089, cutscene frame 428/442, cutscene commands 421/435 and Camera
274/284. Source-closed owners are 3/8; runtime-import-ready owners remain 1/8.

## 4b0b39f hard-float and packet closure wave

The current immutable package is:

```text
I:\oot3dre_work\source_integration\4b0b39f9e705-4a77de5d1200
```

The producer promotes 100 complete target bodies: 95 bulk-only and five
certified identities. Fifteen intersect configured owner requests and 85
advance the target-wide registry. Twelve are direct closure members, adding
32 weighted memberships; `004BC720`, `004C6D34` and `004C87F0` are indirect
requests, so their eight owner references leave the worklist without
inflating direct-closure coverage.

Weighted direct closure reaches 3,568/3,689 (96.7200%), up 32 memberships and
0.8674 percentage points. Unique direct closure reaches 1,170/1,221
(95.8231%), up 12 identities and 0.9828 points. All 15 owner-facing
promotions leave the ranked worklist, which falls from 67 to 52 entries.

Current effective closure is Player 1,043/1,090, Player-common 1,042/1,089,
cutscene frame 433/442, cutscene commands 426/435 and Camera 275/284. Actor,
renderer and GameState remain source-closed, while only GameState is
runtime-import ready. The remaining aggregate frontier is 47 bulk-only, two
certified, two target-mismatch and one exact-anchor-only request; 51 are
direct members and `004BC22C` is the one shared indirect target.

## 6a1197f runtime-acceleration and hard-float correction wave

The current immutable package is:

```text
I:\oot3dre_work\source_integration\6a1197f3a520-7286a857de90
```

The producer promotes 100 bounded target bodies across 26 maintained modules.
One promoted identity lies in configured owner closures but was already an
accepted compiler service boundary; the other 99 improve the target-wide
registry without changing owner totals. The owner advance instead comes from
the target-backed correction of `0x003404A8` and `0x00358DFC`: both now
receive the hard-float animation speed explicitly, preserve CSAB last-frame
lookup and forward the observed fixed or dynamic caller speeds.

The consumer removes both obsolete `target_mismatch` overrides. This resolves
four weighted memberships and two unique direct entries. Weighted closure is
now 3,572/3,689 (96.8284%) and unique closure is 1,172/1,221 (95.9869%).
The worklist falls from 52 to 50 entries and contains no quarantine: 47 are
bulk-only, two are semantic-certified and one is exact-anchor-only. Forty-nine
are direct closure members and `004BC22C` remains the sole shared indirect
target.

Effective closure is Player 1,045/1,090 (95.8716%), Player-common
1,044/1,089 (95.8678%), cutscene frame 433/442 (97.9638%), cutscene commands
426/435 (97.9310%) and Camera 275/284 (96.8310%). Actor, renderer and
GameState remain source-closed, so source closure is 3/8 (37.5%); only
GameState is runtime-import-ready, 1/8 (12.5%). The remaining 50 requests
should stay aggregated, with the two certified bodies, owner roots and shared
Player/Camera/cutscene dependencies ahead of isolated leaves.

## 6949fa0 residual owner macro-wave

The current immutable package is:

```text
I:\oot3dre_work\source_integration\6949fa01accc-7286a857de90
```

The producer promotes 100 complete bodies, six of which are direct configured
owner members. They resolve 18 weighted memberships; the other 94 advance the
target-wide reviewed registry. Weighted direct closure is 3,590/3,689
(97.3163%) and unique direct closure is 1,178/1,221 (96.4783%). The ranked
worklist falls from 50 to 44 entries: 43 direct requests and the shared
indirect target `004BC22C`. Its status mix is 41 bulk-only, two certified and
one exact-anchor-only.

Effective closure is Player 1,051/1,090 (96.4220%), Player-common
1,050/1,089 (96.4187%), cutscene frame 435/442 (98.4163%), cutscene commands
428/435 (98.3908%) and Camera 277/284 (97.5352%). Actor, renderer and
GameState remain source-closed (3/8); only GameState is runtime-import-ready
(1/8). Keep all 44 residual requests aggregated, prioritizing the owner roots,
two certified bodies, shared hot dependencies and the indirect target.

## 92c4a66 shared owner packet wave

The current immutable package is:

```text
I:\oot3dre_work\source_integration\92c4a669d25a-7286a857de90
```

The producer promotes exactly 100 complete target bodies across 18 maintained
modules. Twelve are direct configured owner identities and resolve 28
weighted memberships; 88 improve the target-wide reviewed registry. Explicit
target-proved packets replace the remaining implicit resource, archive, GPU,
camera-water, input and applet outputs selected for this wave.

Weighted direct closure reaches 3,618/3,689 (98.0754%), up 28 memberships and
0.7590 percentage points. Unique direct closure reaches 1,190/1,221
(97.4611%), up 12 identities and 0.9828 points. The ranked worklist falls
from 44 to 32 (-27.2727%): 31 direct entries and the shared indirect target
`004BC22C`.

Effective closure is Player 1,063/1,090 (97.5229%), Player-common
1,062/1,089 (97.5207%), cutscene frame 436/442 (98.6425%), cutscene commands
429/435 (98.6207%) and Camera 279/284 (98.2394%). Actor, renderer and
GameState remain source-closed, so source closure is 3/8 (37.5%); only
GameState is runtime-import-ready, 1/8 (12.5%). Retain all 32 residual
requests as one aggregate frontier while the consumer advances acceptance
work independently.

## 9ba85ae residual packet closeout

The current immutable package is:

```text
I:\oot3dre_work\source_integration\9ba85aeae92f-7286a857de90
```

The producer promotes exactly 100 complete target bodies across 17 maintained
modules. Seven are direct owner identities and resolve 16 weighted
memberships; 93 advance the target-wide reviewed registry. Weighted direct
closure is now 3,634/3,689 (98.5091%) and unique direct closure is
1,197/1,221 (98.0344%). The ranked worklist falls from 32 to 25: 24 direct
entries and shared indirect target `004BC22C`.

Effective closure is Player 1,070/1,090 (98.1651%), Player-common
1,069/1,089 (98.1635%), cutscene frame 437/442 (98.8688%), cutscene commands
430/435 (98.8506%) and Camera 279/284 (98.2394%). Actor, renderer and
GameState remain source-closed (3/8); only GameState is runtime-import-ready
(1/8). Keep the 25 requests aggregated around the remaining roots and shared
BgCheck, Camera and cutscene dependencies.

## 6e8119d shared camera runtime closeout

The current immutable package is:

```text
I:\oot3dre_work\source_integration\6e8119dbf710-7286a857de90
```

The producer promotes 101 complete target identities across 19 maintained
modules: 91 bulk-only bodies, nine certified bodies and the typed
`Camera_InitPlayerSettings` reconstruction. Four direct owner identities
resolve seven weighted memberships. Shared indirect target `004BC22C` also
closes, so the ranked worklist falls from 25 to 20 (-20.0%) and now contains
only direct entries.

Weighted owner closure is 3,641/3,689 (98.6988%) and unique direct closure is
1,201/1,221 (98.3620%). Effective closure is Player 1,073/1,090 (98.4404%),
Player-common 1,072/1,089 (98.4389%), cutscene frame 437/442 (98.8688%),
cutscene commands 430/435 (98.8506%) and Camera 280/284 (98.5915%). Actor,
renderer and GameState remain source-closed (3/8); only GameState is
runtime-import-ready (1/8). Keep all 20 residual direct requests in one
cross-owner acceptance wave, prioritizing the three roots and shared BgCheck
dependencies.

## b2263fa owner-frontier runtime closeout

The current immutable package is:

```text
I:\oot3dre_work\source_integration\b2263fafb2dd-7286a857de90
```

The producer promotes exactly 100 complete target bodies across 16 maintained
modules. Ten are direct owner identities and resolve 26 weighted
memberships; 90 advance the target-wide reviewed registry. The worklist is
halved from 20 to ten direct entries and contains no indirect, dynamic or
quarantined blocker.

Weighted owner closure reaches 3,667/3,689 (99.4036%), an increase of 0.7048
percentage points. Unique direct closure reaches 1,211/1,221 (99.1810%), an
increase of 0.8190 points. Effective closure is Player 1,082/1,090
(99.2661%), Player-common 1,081/1,089 (99.2654%), cutscene frame 441/442
(99.7738%), cutscene commands 434/435 (99.7701%) and Camera 280/284
(98.5915%).

Actor, renderer and GameState remain source-closed (3/8); only GameState is
runtime-import-ready (1/8). Keep the final ten entries aggregated rather than
splitting them into owner-local queues: eight are shared by Player and
Player-common, one Cutscene root closes both cutscene graphs, and two Camera
dependencies are also shared with Player. Runtime acceptance remains a
consumer-side gate independent of these producer percentages.

## Canonical owner identities and final shared-leaf tranche

The consumer now distinguishes a raw callgraph name from a target-verified
canonical owner identity. This resolves both closed Actor roots:

- `0x00461344`: raw `FUN_00461344`, canonical `Actor_UpdateAll`;
- `0x0044E7C0`: raw `FUN_0044e7c0`, canonical `Actor_InitContext`.

The stable historical ID `game_state_renderer_initializer` remains unchanged
so package history and acceptance records stay comparable.

The same package records the non-contiguous target boundary: the root ends in
a tail branch from `0x0044EFA4` to the separately framed
`0x004632B4-0x0046341F` helper. Source import may keep that helper separate or
inline it only after a Native30 differential; it must never model the
intervening address range as part of the owner.

The consumer-side adapter sequence is now:

1. checked guest-to-host access through `GetReadPointer`/`GetWritePointer`;
2. checked host-to-guest recovery through `GetGuestAddress`;
3. ABI-complete nested dispatch through `NativeA32OwnerCallAdapter`, including
   core, VFP and source-owner stack arguments;
4. target-global and allocator services;
5. typed callback dispatch with ordinary A32 fallback;
6. owner-level Native30 and savestate differentials;
7. explicit acceptance before runtime activation.

The adapter is deliberately owner-agnostic. It captures the current callback
register image, routes callees through the normal process dispatcher and
shares only guest memory effects; it does not copy gameplay state into host
globals or cast target callback addresses to host functions. A reentrant
process test validates this boundary. The next Actor tranche is therefore the
revision-pinned source unit plus service thunks and full-root differentials,
not another partial A32 block translation.

Producer commit `449e219e8a8bdd8025981b0ba5f961f1a6614ea4` makes the
`owner_final_leaf_closeout` tranche authoritative. It promotes 104 bodies,
including four configured owner dependencies. With `sinf` classified as a C
runtime boundary, the unique frontier is five functions and aggregate unique
coverage is 1,215/1,220 (99.5902%). This does not make another owner runnable:
three of eight source graphs are closed and only `GameState_Update` has
consumer runtime acceptance.

The producer should close the five residual identities as one final vertical
tranche. In parallel, the consumer should finish the isolated
guest-pointer/global/callback adapter and Native30 differential harness for
`Actor_UpdateAll`, then reuse that infrastructure for `Actor_InitContext`.
Neither owner may be enabled solely because its source tree materializes.

## Actor source-import Native30 acceptance

The consumer now materializes producer revision
`449e219e8a8bdd8025981b0ba5f961f1a6614ea4` from the immutable
`449e219e8a8b-a6b96b41f301` package. The imported C object remains isolated
behind `oot3d_source_actor_update_all_runtime`; the normal runtime only
registers `0x00461344` when `--enable-source-actor-update-all` is explicit.
Without that switch the existing A32 selection path is unchanged.

The owner-level differential is executable rather than metadata-only. It
compares source and original A32 memory after three Native30 ticks, traverses
all twelve populated categories in exact order, and crosses the real init and
update callback continuations through `NativeA32OwnerCallAdapter`. It covers
nonresident kill, drawn-actor destroy and deleted-actor unlink/overlay release
while executing the original A32 `Actor_Delete`. Process checkpoints are
destructively mutated and restored both after a completed tick and at the
first init/update callback boundary on each execution path.

This work also establishes that ActorContext is embedded at
`PlayState+0x208C`, while `PlayState+0x20AC` aliases the Player pointer and
category-2 list head. Executable fixture maps are kept to the actual 4 KiB
root/lifecycle pages, so the full differential runs in milliseconds rather
than serializing broad empty mappings. Existing dispatcher and typed-gameplay
regressions pass.

The production bridge now routes every imported dependency, dynamic actor
callback and SVC `0x28` through `NativeA32OwnerCallAdapter` and
`NativeA32Process`, using exact guest addresses and ABI stack frames. No
fixture thunk is linked into the game. A 60-frame
`native30_no_interpolation` smoke from
`navi_kokiri_main_forest.oot3dsav` completed with:

- 59 source-owner calls;
- 19,334 ordinary nested guest calls;
- 3,177 actor update callbacks;
- 59 SVC calls;
- zero bridge failures, whole-AOT memory faults or unsupported exits.

The evidence is
`I:\oot3dre_work\native_game\actor_update_all_source_smoke.json`, SHA-256
`d7052debf18b38c953b34c5c56ae811a82e59589cd8ce86ee41d9fd4ff102242`.
The same build without the source-owner switch reaches the pre-existing
unsupported indirect A32 callback at `0x004617A4` because this MSVC build does
not link whole-AOT. The source route crosses that boundary through the
ordinary callback adapter; equivalence to the original root is established
by the byte-level differential rather than inferred from that failed
baseline run.

`source_actor_update_all` in every runtime report exposes activation,
owner/dependency/callback/SVC counts, failures and the last error. Runtime
profiling also reports this owner separately instead of charging it to
`unhandled`.

Native30 activation is therefore accepted as an experimental opt-in. The next
vertical tranche is:

1. run a longer Kokiri interaction/camera transition sequence and retain its
   report as regression evidence;
2. classify Enhanced60 only after that Native30 sequence remains clean;
3. reuse the same pointer, SVC and callback bridge for `Actor_InitContext`;
4. remove the opt-in only after both Actor owners have sustained runtime
   coverage and the release build links the intended fallback backend.

## d5c2927 eight-owner closure and Actor_InitContext consumer import

Producer commit `d5c292708f7f25f921a066449db3cea43fad73c4` closes the
configured source graph for all eight owners. The immutable consumer package
is:

```text
I:\oot3dre_work\source_integration\d5c292708f7f-3819f4804b32
```

Its aggregate semantic closure is 3,676/3,676 weighted memberships and
1,216/1,216 unique functions. The producer worklist is empty and reports no
manual, indirect or dynamic blocker. This completes source discovery; it
does not automatically approve the eight roots for runtime replacement.

The consumer now pins `Actor_InitContext` under
`tools/oot3d/source_imports/actor_init_context_d5c2927`. The manifest records
the producer revision, source-tree hash, `code.bin` hash and both physical
target fragments:

- root `0x0044E7C0-0x0044EFA7`, SHA-256
  `09df5dfd35274cd1f493071bbe2c87cfc5e90ef2143f4dfed73ab0b1b112ebd4`;
- tail helper `0x004632B4-0x0046341F`, SHA-256
  `55824fd0e8c3c6989a61eb9a58e596d3dfb5f2424da490de04fa4a9795b4a3ac`.

`oot3d_source_actor_init_context_runtime` resolves every literal from the
mapped target image at owner entry. Direct services, four renderer-resource
factory families and the allocator virtual call are dispatched by guest
address through `NativeA32OwnerCallAdapter`; no guest vtable pointer is cast
to a host function. The source body keeps the tail helper semantically inline
but records one tail execution per completed owner call.

The target comparison found three ABI details that the producer C still
obscures and that the consumer lowering makes explicit:

1. `Actor_Spawn` receives position in `s0-s2`, `rotX` in `r3`, and
   `rotY/rotZ/params/initializeNow` in four stack words;
2. `Lights_PointNoGlowSetInfo` receives position in `s0-s2`, including the
   target `+80.0f` Y offset, while color uses `r1-r3` and
   radius/attenuation use the stack;
3. the call to `0x00348BE4` observes the newly initialized model object still
   held in `r0`, although the current producer expression has an empty
   argument list.

The decompiler also typed several VFP conversion temporaries as `undefined4`.
The consumer stores the converted attention/light values through `float*`,
matching the target `vstr`, rather than applying a C float-to-integer
conversion.

`oot3d_source_actor_init_context_tests` compares source and generated A32 with
the same deterministic guest services. Both renderer-guard paths pass full
data/RAM byte comparison, Player spawn, 29 dynamic factory calls, one
allocator call, light setup, four material constructions, tail execution and
pre/post-owner savestate round trips. The runtime remains an explicit
`--enable-source-actor-init-context` opt-in until a process-image smoke is
retained.

The practical consumer state is therefore:

- producer source closure: 8/8;
- runtime-accepted roots: `GameState_Update` and `Actor_UpdateAll`;
- differential-complete, smoke-pending root: `Actor_InitContext`;
- remaining roots: source-complete but consumer acceptance not started.

### Process-image boundary found after the import

The first scene-loading smoke used
`navi_kokiri_preload_10380.oot3dsav` with both Actor source owners enabled.
It exposed a broader `Actor_UpdateAll` callback boundary before
`Actor_InitContext` could be observed: deferred Actor initialization invokes
`ObjHana_Init` at `0x001E1734`. That callback is source-recovered in the pinned
snapshot, but it is not linked into this runtime and this build has no
whole-AOT fallback.

The source-owner bridge formerly continued through imported C after such a
nested guest-call failure and eventually dereferenced a null resolver result.
It now aborts the imported owner back to its existing controlled failure
boundary. The unsupported result reports the failing guest address in
`detail`, and `oot3d_source_actor_update_all_tests` forces a dependency
failure to prove that this path cannot become a host access violation.

A second Native30 run from
`navi_kokiri_load_10420.oot3dsav` completed 120 presentation frames and 119
`Actor_UpdateAll` owner calls with zero bridge failures. Its report is
`I:\oot3dre_work\native_game\actor_init_context_source_smoke_10420.json`,
SHA-256
`07c5af58e937e3b537a353c28942c85d5b83f09474320bbefb16791e977430ba`.
It recorded zero `Actor_InitContext` calls because that checkpoint is already
past the relevant initialization boundary. It is composition evidence only,
not acceptance evidence for the new owner.

The next efficient acceptance step is therefore not another arbitrary
checkpoint run. It is either:

1. link the already recovered `ObjHana_Init` callback closure and rerun from
   frame 10380; or
2. capture a checkpoint immediately before `0x0044E7C0`, then run the owner
   without traversing unrelated deferred Actor callbacks.

### ObjHana callback closure and finite-wait composition

The first option is now complete. `ObjHana_Init` is pinned under
`tools/oot3d/source_imports/obj_hana_init_d5c2927` with target body
`0x001E1734-0x001E1817` and exact literal cells from the mapped `code.bin`.
Its local `Actor_LoadModelList` dependency is also source-transferred from
`0x00372F38-0x0037305B`. The implementation preserves the target object-slot
lookup, renderer guard, model factory context, `ZAR_GetCMBByIndex` call,
virtual factory dispatch, statistics order, init chain, hard-float scale,
cylinder setup and event-bit kill path.

`SourceActorCallbackRuntime` is an owner-independent callback registry. The
`Actor_UpdateAll` bridge queries it before falling back to the guest callback
address, so further recovered actor callbacks can be added without embedding
actor-specific branches in the owner body. Differential coverage executes
selectors 0, 1 and 2, both event outcomes, the original A32 model-loader path,
the source model-loader path and a forced dependency failure.

The process-image run then exposed a runtime composition boundary rather than
an `ObjHana` semantic gap. Native `ZAR_GetCMBByIndex` reaches CMB setup, whose
target code performs finite `SleepThread(100000 ns)` calls after GPU work.
`InvokeFunctionWithState` previously rejected every host `Wait` because its
source-owner caller remains on the C++ stack.

The common host contract now permits only a wait that the host can complete
atomically. The CTR implementation accepts a positive finite `SleepThread`
with an already registered timer, removes that timer, advances the emulated
CTR clock to its deadline and wakes other timers that mature at the same
point. It preserves the current guest thread identity. Infinite sleeps,
events, mutexes and all externally signalled synchronization remain owned by
the normal scheduler and fail closed in a synchronous owner call. No
`ObjHana`, ZAR or CMB address is special-cased in this mechanism.

The following tests pass:

- `oot3d_native_a32_process_tests`;
- `oot3d_native_a32_ctr_host_tests`;
- `oot3d_source_actor_update_all_tests`.

The retained Native30 process report is:

```text
I:\oot3dre_work\native_game\actor_init_context_source_smoke_10380_objhana.json
SHA-256 44dcac6bea9a601cc0f0bde8b412bcea44181d0b9f3f5cf23b25d932f63ac8ef
```

It completes 120 frames with 119 source `Actor_UpdateAll` calls, 36,056
nested guest calls, one source `ObjHana_Init` call, 150 owner SVC calls and
zero owner or callback failure. `Actor_InitContext` remains at zero because
the restored checkpoint is already past that context initialization. Its
next acceptance action is therefore option 2 above: capture a checkpoint at
the `0x0044E7C0` entry boundary and run the existing differential-complete
owner from that state.

## Actor_InitContext process acceptance

`Actor_InitContext` is now the third runtime-accepted owner. The retained
frame-12000 checkpoint enters `0x0044E7C0` with the target call-site ABI:
`r0=PlayState`, `r1=PlayState+0x208C` and
`r2=*(PlayState+0x5C0C)`. The 600-refresh Native30 run completes one full
owner call, 86 nested guest calls, 29 dynamic factory calls, one allocator
call, one light setup and the tail helper with zero failure.

The process run found two contracts that the fixture differential alone could
not expose:

1. bulk renderer initialization at `0x004644A8` legitimately crosses the host
   dispatcher more than 1,024 times while executing its bounded model and
   material loops; the owner adapter now carries an explicit per-call limit,
   and only this bulk dependency receives the larger budget;
2. the material constructor at `0x0034897C` receives four core-register
   arguments. The target call at `0x0044EF54` clears both `r2` and `r3`; the
   producer-derived three-argument expression left `r3` stale and incorrectly
   activated optional transfer-state allocation.

Finite synchronous CTR sleeps during renderer creation can also advance the
emulated clock past an already scheduled VBlank. Deadline resolution is now
monotonic: an overdue VBlank is delivered at the current tick and later
deadlines catch up, without moving time backwards or dropping the display
event. The acceptance run observed five late deadlines, a maximum overshoot
of 19,715,893 ticks (73.536 ms), and exact clock/deadline alignment at exit.

Evidence:

```text
I:\oot3dre_work\native_game\actor_init_context_source_smoke_frame12000_accepted.json
SHA-256 bab3cbe1e70352747c4ac5a472dbae6f3efab3f0737ddf76cdd101b35d2e886a
```

The corresponding content-addressed owner package is
`I:\oot3dre_work\source_integration\d5c292708f7f-4adc6afbb839`.
It keeps producer commit `d5c2927` and all eight closed source graphs
unchanged while reporting three hashed runtime-import acceptance records.

Consumer runtime status is now 3/8 accepted:
`GameState_Update`, `Actor_UpdateAll` and `Actor_InitContext`. Activation of
both Actor owners remains explicit while longer interaction and Enhanced60
classification are pending. The next vertical import should target the
shared cutscene pair before Camera or Player, because it closes a coherent
control-flow boundary and exercises the already recovered scene/actor
composition without first taking on either large Player graph.

## Cutscene_UpdateFrame differential import

The first cutscene owner is now transferred without importing its 442-function
closure wholesale. `Cutscene_UpdateFrame` at `0x00321F50` is a 292-byte
orchestrator with three direct dependencies:

| Dependency | Entry | Current execution |
| --- | --- | --- |
| backend-clock query | `0x002C2D78` | measured guest call |
| backend-clock commit | `0x0048B198` | measured guest call |
| `Cutscene_ProcessCommands` | `0x002C5BA0` | measured guest call |

The revision-pinned import is
`tools/oot3d/source_imports/cutscene_update_frame_d5c2927`. Its target body
hash is
`bb317ded32373e22fc7aa7b775eb794455808f381868228c7dbe904f55ec3430`.
All five literal cells are loaded from the mapped `code.bin`; no scheduler,
clock scale, frame rate, PlayState offset or command data pointer is supplied
by host policy.

`oot3d_source_cutscene_update_frame_tests` compares the source owner with the
generated A32 owner for scheduler rejection, scene-mode rejection, both clock
handshake outcomes, first-frame state clearing, negative-clock normal
advance, multi-frame catch-up and an already reached target. Every scenario
compares all mapped data/RAM, dependency order and ABI, the exact frame seen
by each parser call, then restores and replays independent process
checkpoints. A ninth case above `2^24` also compares guest FPSCR exception
propagation, using the runtime's software-exact VFP operations rather than
host floating-point behavior. All scenarios pass.

The owner is now accepted as an explicit Native30 opt-in through
`--enable-source-cutscene-update-frame`. Enhanced60 rejects that switch and
retains its existing typed frame-crossing owner; source correctness does not
silently change the unlocked timing policy.

The retained Link-dream process run completes 300 refreshes with 299 source
owner calls, 299 measured calls into the original
`Cutscene_ProcessCommands`, zero failures and zero late VBlank deadlines.
The same checkpoint and scheduling contract through the current typed
baseline produces the identical memory-content fingerprint
`15526982841859449935`, final guest tick `15546019117`, Actor owner count and
Actor callback count. The host memory-state fingerprint is intentionally not
an equivalence key because it additionally hashes `mWriteGeneration`, an
invalidation counter whose value depends on how many host write operations
an implementation uses even when final guest bytes are identical.

Evidence:

```text
I:\oot3dre_work\native_game\cutscene_update_frame_source_smoke_link_dream.json
SHA-256 e59ca5d4ee2c40ce6a7c482021a69c03c7560632177843f2f02a8eb24a810f75

I:\oot3dre_work\native_game\cutscene_update_frame_a32_baseline_link_dream.json
SHA-256 10a81e87b63a156251008538d385691aeca7f84cb133390a3f1d66d3a703f322
route: current typed Cutscene_UpdateFrame baseline
```

The differential also compares the AAPCS-preserved core and VFP state,
stack, return PC, thread pointer and FPSCR against generated A32 on every
branch and after checkpoint restore/replay. `Cutscene_ProcessCommands`
remains the next independently promoted owner; it has not been folded into
this 292-byte import.

The regenerated immutable package is
`I:\oot3dre_work\source_integration\d5c292708f7f-ca8c22db1095`: all eight
producer graphs remain closed, four owners are runtime-accepted and the
worklist remains empty.

## Cutscene_ProcessCommands acceptance

The second cutscene owner is now runtime-accepted, bringing the consumer to
5/8 accepted roots. The revision-pinned import lives in:

```text
tools/oot3d/source_imports/cutscene_process_commands_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_cutscene_process_commands_runtime.*
tools/oot3d/native_game_runtime/oot3d_source_cutscene_process_commands_tests.cpp
```

The import retains the complete `0x002C5BA0-0x002C82D3` body, 69 literal
cells, target-width guest pointers and measured target-address dependency
calls. Its generator also performs one required control-flow lowering:
`default_oot3d_dup3:` is attached to the outer C++ switch as its real
`default:` handler. A two-command differential proves the generic
counted-record stride and prevents the command count from being reinterpreted
as a command ID.

The process run additionally proves the camera-track ABI at
`FUN_0033cb90`: the local three-word state is copied into guest scratch before
the A32 call and is not copied back. The dependency writes its result through
the original `csCtx + 0x94` argument.

It also proves an omitted-argument ABI issue in the producer source.
`FUN_003665fc` consumes `r0-r2`: direct `code.bin` disassembly gives
`(0xE, 1, 1)` at owner call `0x002C61C0` and `(0xF, 0, 0)` at
`0x002C61E8`. The consumer generator restores those explicit third
arguments. The differential fixture exercises the second path and records
all three core registers, preventing a host-register residue from becoming
guest state.

Activation remains explicit and Native30-only. Acceptance covers both
Native30 presentation modes; the retained deterministic evidence uses
`native30_no_interpolation`:

```text
--enable-source-cutscene-process-commands
```

Enhanced60 keeps its typed cutscene timing path. The retained acceptance
reports are:

```text
I:\oot3dre_work\native_game\cutscene_process_commands_a32_baseline_link_dream_accepted.json
SHA-256 9004795e6a67e602de973736aaba89b83a4d74cf47abfb9c7a08168df40e4372

I:\oot3dre_work\native_game\cutscene_process_commands_source_smoke_link_dream_accepted.json
SHA-256 ddfeb034745612f83e3d22bafa33d75efcb44896b3dbce46313ba2a5b50419d9
```

The fixed-delta pair is exact at guest refresh 2,939 for guest-memory content,
system ticks and DSP PCM. The source side completes 29 parser-owner calls,
295 measured nested dependencies, 7,195 reads, 120 writes, 27 scratch calls
and zero failures. Only host operation/write-generation fingerprints differ.
Together with the six-scenario unit differential, this closes the owner
without relying on a visual comparison.

The next vertical owner should be selected from Camera or Player based on the
smallest complete runtime boundary; no new producer closure package is
required.

## Camera_Update acceptance

`Camera_Update` at `0x002D84C4-0x002D948C` is now the sixth runtime-accepted
owner. Its revision-pinned import and runtime boundary are:

```text
tools/oot3d/source_imports/camera_update_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_camera_update_runtime.*
tools/oot3d/native_game_runtime/oot3d_source_camera_update_tests.cpp
```

The owner preserves the six-byte hidden-sret ABI, target literal cells,
hard-float arguments and HFA `Vec3f` returns, stack/scratch arguments, and
target-address camera-mode dispatch. Host arithmetic is enclosed in the
rounding mode selected by FPSCR `RMode`; this is required for bit equality
with the ARM `vcvt`/multiply path and is compiled with strict floating-point
semantics.

Process acceptance exposed a concrete producer-source mismatch. At
`0x002D87CC`, `FUN_0047bff8` returns a background-camera data index in `r0`.
The target saves it in `sl`, then passes that value in `r1` to
`FUN_002d064c` at `0x002D87E8`. The maintained C passed `floorPoly` instead,
which made the resolver index arbitrary memory. The reproducible consumer
lowering substitutes `bgCamIndex`; a forced floor-camera differential records
all three registers at both calls and compares them with generated A32.

Activation is explicit and Native30-only:

```text
--enable-source-camera-update
```

TopScreen/free-camera ownership still has higher dispatch priority, and
Enhanced60 retains its typed camera timing path. Final deterministic evidence:

```text
I:\oot3dre_work\native_game\camera_update_a32_baseline_link_dream_accepted.json
SHA-256 7da50afaed77e8c4c54012c9742b04332fc3c1f5d10661f88dcb91a605c2dbbd

I:\oot3dre_work\native_game\camera_update_source_link_dream_accepted.json
SHA-256 4c26d89c47b3dca8952f70976c7b216eddc9d56861c50533791ba373f53994e3
```

Both paths finish at guest refresh 2,939 and tick `13133012413`, with exact
guest-memory content (`13455304922352739916`) and DSP PCM
(`8058772573127251443`). The source path executes 58 owner calls, 632 measured
dependencies, 29 dynamic camera-mode callbacks and zero failures. The five
scenario differential, guest-pointer test and generated-source reproducibility
check all pass.

Consumer runtime status is now 6/8. The remaining vertical roots are
`Player_Update` and the candidate `Player_UpdateCommon`; Camera does not need
another closure package or visual gate.

## Player_Update acceptance

`Player_Update` at `0x001E1B54-0x001E1DC8` is now the seventh
runtime-accepted owner. Its revision-pinned import and runtime boundary are:

```text
tools/oot3d/source_imports/player_update_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_player_update_runtime.*
tools/oot3d/native_game_runtime/oot3d_source_player_update_tests.cpp
```

The generated owner preserves all 13 target literal cells, checked
target-width guest access, the conditional two-byte `STRH`, target binary32
arithmetic and float-to-signed conversion, and the hard-float `Actor_Spawn`
ABI. The bridge also reproduces the exact `0x60`-byte owner frame: the
48-byte `Input` packet begins at owner `SP+0x10`, and nested dependencies
observe the same stack pointer as the ARM body.

The six-scenario differential covers unavailable object state, actor spawn,
stale actor links, input pass-through, input suppression and blocked input.
It compares all mapped RAM, dependency order and arguments, callee-saved
core/VFP registers, FPSCR and return state against generated A32.

Activation is explicit and Native30-only:

```text
--enable-source-player-update
```

Enhanced60 keeps the existing typed temporal owner. Final deterministic
evidence:

```text
I:\oot3dre_work\native_game\player_update_a32_baseline_link_dream_accepted.json
SHA-256 6cb820acbf2ab58f1d4fe64a22d2a36cf85a05ac1f593d61925c48aeb386c208

I:\oot3dre_work\native_game\player_update_source_link_dream_accepted.json
SHA-256 df52b3030e99fea98654ff4293895d0a10251bdb17995de750d47bfe97e5dc61
```

Both paths finish at guest refresh 2,939 and tick `13133012413`, with exact
guest-memory content (`13455304922352739916`) and DSP PCM
(`8058772573127251443`). The source path executes 29 owner calls and 29
measured `Player_UpdateCommon` dependencies, with 261 reads, 203 writes, 29
scratch packets, 87 target VFP operations and zero failures.

Consumer runtime status is now 7/8. `Player_UpdateCommon@0x00250AD0` is the
only remaining vertical owner; it stays independently measurable rather than
being hidden inside this smaller import.

## Player_UpdateCommon acceptance

`Player_UpdateCommon@0x00250AD0` is now the eighth runtime-accepted vertical
owner. Its revision-pinned import and runtime bridge are:

```text
tools/oot3d/source_imports/player_update_common_d5c2927
tools/oot3d/native_game_runtime/oot3d_source_player_update_common_runtime.*
```

Activation is explicit and Native30-only:

```text
--enable-source-player-update-common
```

The generated owner retains the full 10,588-byte body, 136 literal cells,
88 direct dependencies, dynamic Player action dispatch, exact target frame
identity and FPSCR-sensitive operations. The import generator also records
two target-proven corrections:

- `DAT_00251304` is a `short*`; its source offsets must retain element
  scaling.
- Guest double dereferences use `GuestIndirectRef<T>` so the inner target
  pointer is loaded even when the inner and outer scalar types are identical.

The latter correction was required by an active-gameplay differential rather
than inferred from a visual result. Before the fix, the source path differed
from ARM by exactly one byte at `Player+0x1700`. After the general lowering
fix, both a 60-frame Link-dream pair and a 180-frame ladder/gameplay pair have
identical guest-memory content, Player state, tick schedule, renderer work
and DSP PCM. The gameplay run covers 90 owner calls and 3,512 measured
nested calls with zero failures.

Formal evidence and all source hashes are in:

```text
tools/oot3d/source_imports/player_update_common_d5c2927/import_manifest.json
```

The vertical migration milestone is therefore 8/8 runtime-accepted. The next
consumer phase is composition: expose the accepted owner set as one
measurable Native30 source-gameplay profile, then use remaining A32 execution
as a hotspot inventory. That phase must preserve the individual owner
switches and differential reports so regressions remain bisectable.

## Native30 composed source-gameplay profile

The accepted owner set is now exposed through one opt-in:

```text
--enable-source-gameplay-profile
```

The profile is a selection layer, not a merged owner. It enables the seven
independent source bridges for `Actor_InitContext`, `Actor_UpdateAll`,
`Cutscene_UpdateFrame`, `Cutscene_ProcessCommands`, `Camera_Update`,
`Player_Update` and `Player_UpdateCommon`; the already accepted typed
`GameState_Update` is the eighth root. Every individual source-owner switch
remains available for differential bisecting.

Selection lives in the renderer-independent
`oot3d_source_gameplay_profile.h` module. The runtime rejects the composed
profile under Enhanced60 or when typed/compiled gameplay is disabled, rather
than claiming an incomplete eight-owner path. Reports expose the requested
state, every selected root, the enabled count and the completeness result in
`source_gameplay_profile`; `frame_rate.source_owner_graphs_complete` carries
the same active-owner count.

The profile and the seven explicit switches were compared from
`link_dream_start.oot3dsav` for 60 fixed-delta presentation frames. The two
runs have identical full guest-memory, write-state and process-state
fingerprints, system ticks, framebuffer state and all source-owner counters.
Both submit 2,530 draws and record 180 PICA completions. DSP output is also
identical at 205 frames and PCM FNV-1a
`4924382187376014073`; host output-queue pacing remains a real-time metric.

Retained profile evidence:

```text
I:\oot3dre_work\native_game\source_gameplay_profile_link_dream_accepted.json
SHA-256 ce16eabbd911aa4a62a8c2d74a5af9c2d539bb40f9a8d9fbbc63f548ab55a5e5
```

The report records `enabled_owner_count=8`, `complete=true`, memory content
fingerprint `2319055162437664213` and system tick `13137480944`. The next
step can therefore profile remaining A32 work from one reproducible composed
configuration without weakening the per-owner acceptance boundary.

## CSAB curve leaf and end of leaf-by-leaf migration

The two native CSAB curve evaluators at `0x003084E8` and `0x003087A4` are now
promoted in the hot source overlay. The composition still keeps whole-AOT
fallback for all unpromoted entries. The import is revision-pinned and
preserves checked native curve reads, FPSCR behavior, target-visible stack
writes and the final `CPSR` owner. Both S16 Hermite callsites branch to
`Math_TanF@0x003555D8`; the implementation reproduces that native helper's
range reduction, classifier and VFP polynomial rather than using host `tanf`.

The bounded differential probe covers live calls from the frame-10324 saved
state. It reported `1003/1003` handled calls, `45` S16 calls, `958` F32 calls,
zero fallbacks and zero differential mismatches. The loader test covers
linear, step, Hermite, terminal and malformed curves. Incremental C++ rebuild
time for this owner is about three seconds on the pinned fast overlay build.

This is a completed vertical owner promotion, not a reason to remove the
whole-AOT fallback. Further work should promote the next complete visible
subsystem through the same boundary, preserving boot and bounded verification.

Formal provenance and retained report hashes are in:

```text
tools/oot3d/source_imports/csab_curve_eval_d5c2927/import_manifest.json
```
