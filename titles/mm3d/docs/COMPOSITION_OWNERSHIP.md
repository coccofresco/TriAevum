# Native composition ownership (guarded EUR image)

**Latest receipt: implementation `2ce2f911`; F03/F18 still incomplete.**
The reduced-resolution NIS HUD blocker is closed for the measured Woodfall slice:
1,069/1,078 sampled label pixels match Off exactly; the remainder differ by at
most one RGB8 level, with and without GTAO. Scene raster stays512x853; native UI
raster is768x1280. This does not close the full title/provider/mode inventory.
Latest evidence: `COMPOSITION/OUTPUT_UI/`; older qualification sections below are
historical. Both changed slices have exact three-frame OoT O2 hash preservation.

The adapter observes native producers and consumers; it does not replace guest
code. `mm3d_lighting_hook_sites.h` guards every observed block and requires its
registry entry. Additional data/code guards bind semantic actor owners to this
image. A failed guard disables the observer, not native rendering.

## Deferred resource-lifetime validation

The CMB model's destructor at `5EC220` releases seven binding handles at
`+90..+A8`, including both renderers. These loads and release calls are guarded.
Temporal lifetime now snapshots all seven handles, rather than only the two
renderers, and revalidates the model ABI/handles at deferred consumption and
publication (model start/end, primitive start/end, pending primitive flush).
A changed or unreadable binding revokes temporal metadata only: native commands,
actor attribution and appearance remain intact. The observed old epoch is retired
so a later A→B→A handle sequence cannot revive history. Actor/model destruction
also revokes an open scope or pending primitive at publication. Already published
samples are immutable.

The regression fails on the preceding implementation when the primary renderer
changes between enqueue and consumption. It covers all seven handles plus the
vtable, changes during drawing, resource-only enqueue changes, continuity, observed
ABA, model/actor destruction, and primitive/pending publication. Two read-only
savestate scans corroborate readable CMB-owned handles in Clock Town and Woodfall;
they are not evidence of a live rebind. Unobserved in-place resource recycling at
the same addresses still needs its own native lifetime proof. No texture hash,
scene whitelist, new hook PC or guessed geometry identity is introduced.

Evidence: `COMPOSITION/LIFETIME/`. F03/F18 remain open beyond this bounded defect;
provider/UI acceptance results are recorded in the external lane reports.

## Temporal models

- The three actor-init dispatches (`1749C4`, `174EBC`, `191200`) and actor destroy
  (`191238`) invalidate that actor's queued ownership and model lifetimes.
- The guarded model vtable `63BED4` names destructor `5EC220`; its entry invalidates
  every queued use of the model. Other model ABIs get no temporal contract.
- Actor draw `182C44`/return `182C48` establishes the producer owner. Native queue
  append `224104` records the actual slot/model under the three proved scene
  append callers. Consumers `5EF094`/`5EF0CC` reconcile a complete same-generation,
  same-list permutation; conflicting owners never obtain occurrence identities.
- Scene-owned models use the native Play/scene owner with a separate high-bit
  namespace, not a fabricated actor. Play/scene changes, frame rollback and
  state restore clear ownership. The host lifetime allocator never rewinds.
- The adapter supplies owner, model instance, pass and lifetime. The frontend's
  **default-off** persistent-command-buffer contract fills part and draw offset
  only for inherited jumps outside the transient submitted arena. Direct arena
  draws without the explicit primitive contract below fail closed. Repeated
  identical source draws remain duplicates for the
  existing temporal consumer to reject.
- Published packets are immutable. Lifecycle invalidation prevents later queued
  attribution; it does not rewrite an already submitted historical sample.

## Appearance and overlays

En_Mag is MM3D actor `87`, not the N64 ID `C5`. Its profile at `67C9E8`, draw slot
`67CA04`, callback `39F068` and callback instructions are guarded. The native
actor/model draw receives isolated lighting, including when consumed by the 2D
scene. It keeps its own native lights instead of borrowing scene illumination.
The 2D enqueue helper `11F000` tail-calls `224104` with its caller's LR;
its guarded `11F034..11F044` tail binds the exact model/list/return tuple.
An unmatched tuple acquires no owner. Semantic callback ownership is independent
of the CMB vtable required for temporal identity, including non-CMB atmosphere.

Object_Kankyo is MM3D actor `41` (N64 `51`); profile `689C3C`, draw slot `689C58`
and callback `4C03F4` are guarded. Its known queued models and the existing sky
scope are atmosphere, not surface-lighting receivers.

Native opaque/translucent lists, 2D scene, layout consumers and quad queues 4/5
retain their existing native scope boundaries. The three proved projected-shadow
submission callers remain shadow-only and never acquire actor temporal identity.
Unproved primitive ABIs, source ownership and queue permutations deliberately
retain fallback behavior; no actor/material/occurrence heuristic fills a gap.
Queues 0–3 are **not** newly classified merely from their draw order.

MM UI/isolated overlays opt into `ExcludeFromPresentationFxaa`. At a proved
pre-overlay anchor the existing scene-composition pass runs its existing FXAA
shader before native UI. A content-version serial prevents final scanout from
filtering those pixels again; overlay writes retain it, world writes invalidate
it. Without a proved boundary, presentation FXAA is omitted rather than applied
to the UI. OoT producers leave the new flags false.

`PreserveNativeUiSampling` now permits a proved top-screen before-UI boundary to
reconstruct the reduced scene into an output-density native companion. Subsequent
owned native UI draws retain their original blend/coverage/depth/stencil behavior
on that companion; final scanout does not reconstruct or filter them again.
Separate surface registries prevent an identical guest key from retiring live
scene wrappers. Exact content serials reject stale companions after scene writes;
restore/target teardown destroys them. The changed-pixel overlay remains the
legacy path, not the source of recovered native glyph detail. See
[the renderer contract](../../../docs/NATIVE_UI_OUTPUT_RESOLUTION.md).

The output-density implementation changes no shader producers/packs. All eight
packs match40fcc838; the earlier native-overlay shader change (entry16 of104)
remains preserved, as do the other legacy families. No GP profile was changed.
Runtime qualification and explicit remaining coverage live in the COMPOSITION
lane's external PROGRESS.md / RESULTS.md; unit fixtures alone are not a full-title
or all-primitive runtime qualification.

## Historical qualification receipt (2026-11-07)

The COMPOSITION lane remains a **partial F03/F18 delivery**. CPU suites, the
schedule/bridge regression, 53 native guards, 12 normal-guide compile cases and
shader-family preservation checks passed. The first MM HUD/temporal run failed
on omitted scheduler propagation of the new overlay flag; that defect was fixed
and tested offline, without retrying the consumed gameplay test.

A separate final-source 600-frame MM run completed with GTAO/CACAO, outlines and
HiZ in the declared effect graph, 65 normal-guide draws at each capture and zero
composition/effect-graph failures. Its requested normal-agreement diagnostic was
not visible, so it does not establish numerical normal correctness. No successful
FXAA/upscaler/TAA gameplay acceptance or live title-logo qualification is claimed.

One 400-frame OoT-only Kokiri run passed: all three original BMP hashes match
`I:/_triaevum_work/retuned/cap-p13.sha256` exactly. There were three gameplay
attempts total (two MM, one OoT), no retries. Complete hashes, logs, captures,
remaining gaps and build configuration are recorded under
`I:/_triaevum_work/mm3d-port/analysis/parity/COMPOSITION/RESULTS.md`.

## Authorized continuation (2026-11-07, source `72553f70`)

The 2D helper and non-CMB semantics above have regression fixtures; 59 native
instruction/data guards and registry entries pass. Five CPU suites pass. Shader
producer text and all eight tracked packs remain unchanged. The existing
scanout diagnostic now receives an opt-in, resource-proved availability gate
even when effects were composed before UI; ordinary rendering remains unchanged.

Four new bounded attempts, one per test, with raw output on E: and no retries:

- **HUD boundary execution passed:** 1,000 frames, state-matched Off/FXAA/TAA/NIS.
  FXAA/TAA execute before UI, scanout AA is off, and NIS consumes a scene-only
  snapshot with a separate native UI overlay. Of 1,078 sampled white label
  pixels, 1,074/1,075 respectively are identical under FXAA/TAA; others differ
  by at most one RGB8 level. NIS is **not pixel-identical** (mean difference
  15.46, max97); its separate composition path does not prove unchanged HUD
  appearance under differing reconstruction/resampling.
- **Normal diagnostic now visible:** 600 frames, GTAO then CACAO, outlines/HiZ;
  65 guide draws at each capture and no failed/unresolved graph passes or
  composition mismatches. Each of two 60,000-pixel wall ROIs has no red/blue
  diagnostic pixels and conservative mean positive agreement above0.993.
  The 36,000-pixel floor ROI is positive throughout but has only a0.868
  conservative mean lower bound; do not claim a uniform0.95 threshold or
  full-title/all-material normal qualification.
- **Logo qualification failed coverage:** the 1,500-frame no-input boot stayed
  in the opening cinematic, with zero isolated-model spans. It was not retried.
- **OoT O2 passed:** all three original Kokiri BMP hashes match again on these
  sources. This is a new-source preservation gate, not a confirmation rerun.

F03/F18 remain open: live logo coverage, primitive identity/queues0–3, broad
lifecycle/scene inventory, NIS appearance parity, MM developer-tools-OFF O7 and
owner look approval are not established. Evidence and per-test hashes are in
`COMPOSITION/CONTINUATION/`; earlier receipts above remain historical.

## Primitive and quad contracts added after `72553f70`

`1565AC`/`1565BC` brackets the real builder cursor (`[builder]+8`). Eight guarded
caller returns are recognized. A primitive inherits composition from its exact
active model/quad scope, but obtains temporal identity only when a proved CMB
model epoch, renderer pointer, group descriptor, record index/count/vertex count
and builder linkage all agree. Primary/secondary renderer replacement invalidates
the model epoch. Group and record become Part/Slot; the frontend's transient,
default-off `TemporalPacketSourceRelative` adds the trigger's offset within the
actual called buffer. It requires the draw's own explicit span, never inherits
from a calling jump, and is cleared before publication. Parent scopes cannot
overwrite precise children. Unknown associations stay non-temporal; independent
resource-rebind/full-title coverage remains open.

Quad entries are tracked by exact manager slot and native queue generation.
`112CA4` resets that generation: `Play_Draw` is too late because UI update may
already have enqueued entries. Generic `179930`, flat-color pool `179AD8`, and
textured pool `191CC8` supply their actual manager/queue/model. Guarded native
environment producer scopes can mark atmosphere; queue order alone cannot classify
queues0–3. Ordinary proved 2D insertions retain UI semantics.

`16160C`/`161664` carry the native insertion permutation through ownership metadata,
including identical model handles with different semantic owners. Dekui's guarded
minimap/radar transaction publishes exact source/destination slots after moving
queue6 entries into queue4. Source-slot identities survive its stable sort; no
per-model/occurrence matching is used. Post-write model validation rejects stale
moves. Removed entries are invalidated. The distinct secondary array at+6598 and
unproved queue consumers are not automatically assigned these contracts.

## Latest bounded qualification (`9daec295`)

Six new single-attempt tests, each preceded by a source/configuration change or a
separate gate; failed attempts were retained, not silently rerun:

- Three-scene ownership:720frames, no range/composition errors, but failed the
  temporal-primitive/UI coverage criteria. It exposed queue lifetime coverage.
- Corrected-input logo:1800frames, visible logo,1122 isolated model spans,
  719 atmosphere quad spans and70 temporal primitive spans; invalid ranges0.
  Unknown quad entries and2871 primitives outside proved scopes remain explicit.
- Overlay after first sampling change:1000frames; all1078 selected FXAA label
  pixels match Off, TAA differs by at most1. NIS appearance still failed.
- Relocated-UI/O7:600frames with developer tools OFF;198 native UI moves and198
  UI quad spans, unknown quad entries0, invalid owners0. NIS appearance failed.
  This is a bounded OFF-build execution receipt, not the full O7/title matrix.
- Fractional-grid UI:600frames, tools OFF; direct-grid execution proved for
  fit `(0.25,0,1279.5,720)`. NIS label mean absolute RGB8 error13.988, max97,
  only124/1078 identical. **Pixel acceptance failed despite correct execution.**
- Final OoT O2:400frames, all three original Kokiri BMP hashes match exactly.

Nine CPU suites and125 instruction/data guards pass. Builtin shader check covers
104 entries; exactly the overlay entry changed. Earlier numerical guide evidence
still qualifies only measured walls, not the floor/all-materials. No new guide
run is claimed here. Broad scene/lifecycle/atmosphere inventory, unknown primitive
and queue associations, full-resolution upscaler UI, full normal qualification,
full title matrix and owner look approval remain open.

The assigned private-NRI build was restored and rebuilt dual-title
(`oot3d;mm3d`, developer tools ON, registryv10), without a confirmation launch.
Exact binary hashes, attempt markers, measurements and current remaining work:
`I:/_triaevum_work/mm3d-port/analysis/parity/COMPOSITION/RESULTS.md`.
