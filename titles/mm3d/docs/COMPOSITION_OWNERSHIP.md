# Native composition ownership (EUR revision 0)

The adapter observes native producers and consumers; it does not replace guest
code. `mm3d_lighting_hook_sites.h` guards every observed block and requires its
registry entry. Additional data/code guards bind semantic actor owners to this
image. A failed guard disables the observer, not native rendering.

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
  draws fail closed. Repeated identical source draws remain duplicates for the
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
to the UI. Upscalers continue using the existing scene-before-UI snapshot and
native overlay contract. OoT producers leave the new flag false.

No shader source/pack or release environment profile changes are required.
Runtime qualification and explicit remaining coverage live in the COMPOSITION
lane's external PROGRESS.md / RESULTS.md; unit fixtures alone are not a full-title
or all-primitive runtime qualification.

## Qualification receipt (2026-11-07)

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

### Static primitive investigation, not a temporal contract

`1565AC` forwards to `156F98`, which emits one indexed primitive through a
builder-owned cursor (`[builder] +8`), not necessarily the frame cursor
`6A3A88`. Renderer callers `5E32F4`, `5E4214`, `5E920C`, `5EA1C8` traverse
native group descriptors in r7, count at+8, eight-byte records at+16, index r5;
`5E3208`/`5E9120` use r8/r6. Their record data is not a stable owner by itself:
prebuilt-buffer construction, model/resource lifetime and source-buffer reuse
still need a proved association. No proximity, material or occurrence fallback
has been added.

Quad producer `179930` and generated-quad helper `1799E4` append to manager
queues at+578 with600-byte stride and180-entry capacity (hex). Consumer
`12C6A8` also visits a different array at+6598 before that primary array.
Frame wrappers `120BE8`, `120BF0`, `120BF8`, `120C08` select queues0–3.
Shared manager storage or draw order alone does not prove UI/atmosphere ownership;
these observations do not close their classification gap.
