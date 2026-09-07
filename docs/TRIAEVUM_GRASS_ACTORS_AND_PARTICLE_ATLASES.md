# Grass actors and native particle atlases

## Root causes and ownership

- Grass had a single `PublishLink`/`LatestLink` endpoint with no live producer in
  the current runtime. Its direct collision and recovery field therefore had no
  gameplay data. The old single-Link API remains only for compatibility tests;
  production uses complete actor-frame snapshots.
- Visual interpolation treated every non-unsigned-byte vertex attribute as a
  continuous number, including texture coordinates and floating-point bone
  selectors. UVs selecting consecutive atlas images must not traverse the space
  between those images. This produced incorrect particle silhouettes in x2/x3.

The implementation does not replace dust, create new particle assets, change
guest animation timing, or tune the geometry of Link/Epona individually.

## Actor adapter

`tools/oot3d/native_game_runtime/oot3d_native_actor_interactions.cpp` reads the
existing, locally imported native layout in
`tools/oot3d/source_imports/actor_update_all_449e219e/include/oot3d/actor{,_spawn}.h`.
No decompilation repository was changed or resynchronized.

- PlayState ActorContext: `+0x208C`; all 12 category lists, bounded and cycle checked.
- Actor: world position `+0x28`, collision info `+0xA0`, next link `+0x130`.
- Contact footprint: native cylinder radius/height/Y shift, not model scale,
  guessed render bounds, or a title-specific renderer table.
- Live, drawable actors with positive cylinder dimensions are published.
  Controller-only objects and invalid/dead records are excluded. A non-cylinder
  actor needs a future adapter for its actual native collision shape, not a
  fabricated cylinder.
- The existing scene-view probe supplies PlayState. Capture occurs at completed
  visual source frames. The bridge retains four source snapshots and samples the
  same previous/current source IDs and alpha used by the geometry replay.
- Identity/context changes, teleports, missing frames and despawns cannot leave
  a stale contact or sweep across a whole scene. Savestate reset clears history.

The renderer knows no PlayState offset or actor ID. `GrassInteractionBridge`,
`GrassInteractionField`, `GrassGpuInstanceCompactor` and the CPU fallback operate
on world-space actor colliders. GPU storage is 48 bytes per actor per frame slot;
the existing static anchor cache stays unchanged. Swept-box rejection precedes
the more expensive direct-collision math. Accumulated bend is bounded equally
on CPU and GPU. The finite recovery field follows motion; direct contacts also
work outside this field. Spawn/teleport of one actor does not clear other trails.

F1 / Grass / Interaction now reports **Active actor colliders**. Existing
configuration files retain the `Grass.LinkInteraction` key for compatibility;
its controls now apply to all published actors.

## Temporal material policy

`runtime/three_ds_recomp/include/fast/renderer3ds/pica_vertex_temporal_policy.h`
classifies discrete inputs using recovered shader hooks, not attribute numbers
or particle/asset names. The visual-frame consumer uses the policy in both
change detection and interpolation, including prepared/view samples.

- Texture-coordinate source inputs retain the authoritative current value.
- Bone-index inputs also remain discrete, regardless of storage format.
- Positions and continuous render uniforms still interpolate; material matrix
  animation remains independent of atlas frame selection.
- An unclassified shader retains the previous policy; additional shader shapes
  should extend hook analysis, not add scene exceptions.

## Verification (2026-09-07)

- 305 grass/foundation/graph tests passed, including new multi-actor,
  interpolation, duplicate sample, despawn and teleport regressions.
- `oot3d_native_pica_vulkan_plan_tests` passed, with atlas preservation at alpha
  0, 1/3, 1/2, 2/3 and 1 while a separate geometry input continues moving.
- F1 smoke passed 1,439 assertions using the real renderer, Controls and
  TopScreen widgets.
- Real title playback reports Epona's native radius 35 and height 100. Link and
  other category-list actors are observed without hardcoded identity selection.
- Real visible-anchor probes recorded 4/7/10 nonzero contacts at presentation
  frames 480/540/600, with bend reaching the configured 0.85 limit. GPU compaction
  remained active; no grass generation/render errors occurred in the inspected run.
- The native LA8 dust texture is a 128x128, 4x4 atlas. Its captured shader hooks
  identify texture inputs 3/4/5. Native UV records include a cell spanning
  `[0.75,1] x [0,0.25]`. Filtering remains the original bilinear filtering.
- Framebuffer captures before/after the UV fix show the rectangular dust artifact
  removed at the inspected title view (frame 2520). These captures are not a
  claim of pixel-exact parity across every scene or a new Azahar comparison run.

Private evidence under `I:/oot3dre_work/`:

- `grass-actors-dust-native-baseline-20260907`: toon/AO/grass disabled control.
- `grass-actors-dust-integrated-20260907`: actor bridge, before atlas correction.
- `grass-actors-dust-verified-20260907`: live collider/contact probes, texture dump.
- `grass-actors-dust-textures-20260907`: native textures in Azahar dump format.
- `grass-actors-dust-atlas-fix-20260907`: corrected x2 playback, framebuffers and
  limited semantic trace. No user configuration or saves were overwritten.

These are paced screenshot runs, not uncapped FPS benchmarks. Do not compare
their aggregate FPS as a performance result.
The final diagnostic recorded grass GPU cost 0.93 ms mean / 1.70 ms p95 and
steady CPU cost 2.56 ms mean / 3.53 ms p95; these describe that run, not a matched
before/after speedup. Static placements were built three times at startup and
never rebuilt after all three sources became ready.

## Repeatable diagnostics

`tools/triaevum_release/tests/benchmark_gameplay.mjs` makes private configuration
and save copies, captures the framebuffer and terminates the game at the chosen
duration. `grassDiagnostics=true` adds native actor records and sampled visible
grass contacts; `semanticTrace=true` writes `pica.jsonl`.

Set `OOT3D_PICA_TRACE_FIRST_FRAME` and `OOT3D_PICA_TRACE_LAST_FRAME` to restrict
expensive draw/shader records to a relevant presentation-frame range. Native
float attribute samples and shader texture-input roles are included in those
draw records. Leave both unset for an unrestricted trace. Keep these diagnostics
disabled when measuring performance.

Fast build targets: `triaevum_public_runtime` and
`oot3d_native_pica_vulkan_plan_tests`, parallelism 3. The private title AOT module
does not require recompilation for either correction.
