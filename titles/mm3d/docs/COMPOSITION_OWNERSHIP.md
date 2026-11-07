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
