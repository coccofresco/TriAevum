# OOT3D structural scenario coverage

## Objective

Increase runtime and renderer coverage without manually playing to every
location and without maintaining one savestate per scene. The harness uses one
quiescent gameplay state only as a live process anchor, requests an original
OOT3D entrance through `code.bin`, and leaves scene, room, actor, camera,
lighting, material and cutscene construction to the original game code.

This is not a host-side scene builder. No `PlayState`, actor list or renderer
state is fabricated from the catalog.

## Evidence and generated catalog

`generate_structural_scenario_catalog.py` joins the copied, structured decomp
evidence for:

- the global entrance table;
- scene setups and native spawns;
- actor placements;
- native light records;
- cutscene references;
- the native transition helper contract.

`Update-Oot3dStructuralScenarioCatalog.ps1` is the reproducible entry point.
The generated `oot3d_structural_scenarios.json` currently contains 82 validated
entrance recipes across 27 distinct scenes. Every source file is recorded with
its byte count and SHA-256.

The runtime contract is evidence-backed:

- `FUN_003348E8` requests an entrance at `PlayState+0x5C32` and trigger
  `PlayState+0x5C2D`;
- `FUN_0035DA3C` derives the native transition effect at
  `PlayState+0x5C76`;
- `Save_GetEntranceIndex` reads the 32-bit entrance value at save-context
  address `0x00587958`;
- the active native scene id is the signed 16-bit `PlayState` field at
  offset `0x104`.

The earlier candidate `0x00587960` was rejected: decomp evidence identifies it
as the age field, and live runs kept it at zero while successful transitions
completed.

## Runtime lifecycle

`NativeScenarioBootstrap` implements one generic lifecycle:

1. wait for an observed native `PlayState`;
2. wait until the original transition slot is free;
3. call the native request and effect-preparation helpers;
4. observe the requested entrance after the accepted request, then the target
   `PlayState.sceneId`;
5. require the transition slot to be idle and record the entrance resolved by
   the native loader;
6. settle for the recipe's declared frame window;
7. capture for the declared frame window;
8. complete either after the full capture or when native gameplay requests a
   departure after the full settle window.

The catalog v2 makes both non-trivial policies explicit. Loader normalization
is accepted only after the causal sequence `accepted request -> requested
entrance observed -> target scene observed`; merely seeing the expected scene
id is insufficient. A native departure is accepted only while the resolved
entrance and target scene still match and only after every settle frame. Other
identity changes and premature departures remain failures. Every state change
is retained in the scenario JSON for audit.

`--scenario-auto-exit` stops a bounded run after the full lifecycle, so large
matrices do not pay an arbitrary frame budget. `--scenario-strict` requires
completion rather than merely observing a transition request.

Structural runs intentionally use native gameplay timing and disable the
optional typed-gameplay bridge. That bridge currently redirects camera delay
block `0x002D89D0` to the mid-block continuation `0x002D89D8`, which is not an
entry in the whole-AOT dispatcher. This is an independent bridge integration
defect, not a scene-loading fallback, and must be repaired separately.

## Shader closure loop

`Test-Oot3dStructuralScenarioMatrix.ps1` performs two passes:

1. **Discovery:** load each selected entrance, complete settle/capture, and
   emit the effective vertex, fragment and NRI-fragment source inventory.
2. **Strict:** merge all successful inventories with the checked-in baseline,
   compile one SPIR-V `.o3ps` pack, reload every successful scenario, and
   require zero runtime shader misses.

Failures are isolated. A failing recipe remains failed in the report, but it
does not prevent successful recipes from contributing to the pack or receiving
a strict verification pass.

The native PICA frontend also snapshots all five procedural-texture LUT banks.
When TEV references `Texture3`, the fragment generator decodes the original
ProcTex clamp, shift, combiner, noise, LOD and filter registers and specializes
those LUTs into the offline shader module. LUT contents are part of the
canonical fragment-program identity, so distinct native procedural materials
cannot alias the same AOT module. Ordinary materials do not emit or compile
the procedural GLSL surface.

Selections are:

- `smoke`: Link's House, Kokiri Forest and Hyrule Field;
- `scenes`: one deterministic representative for each of the 27 scenes;
- `all`: all 82 evidence-qualified entrances;
- `-Scenario <id>`: an explicit subset.

```powershell
.\scripts\oot3d\Update-Oot3dStructuralScenarioCatalog.ps1
.\scripts\oot3d\Test-Oot3dStructuralScenarioMatrix.ps1 -Selection smoke
.\scripts\oot3d\Test-Oot3dStructuralScenarioMatrix.ps1 -Selection scenes
.\scripts\oot3d\Test-Oot3dStructuralScenarioMatrix.ps1 -Selection all
```

Reports and per-scenario logs are written below
`I:\oot3dre_work\structural-scenarios\matrices`; `latest.json` points to the
latest aggregate report.

## Validated baseline

The 2026-08-24 smoke run passed all three recipes in six isolated processes:

| Scenario | Native entrance | Scene id | Strict draws | Shader misses |
| --- | ---: | ---: | ---: | ---: |
| Link's House | `0x00BB` | 52 | 10,359 | 0 |
| Kokiri Forest | `0x0211` | 85 | 18,100 | 0 |
| Hyrule Field | `0x0185` | 81 | 18,491 | 0 |

The merged pack contains 105 modules. The selected recipes reference 251 actor
placements, 231 decoded light records and 23 cutscene sources. These are
declared-evidence counts, not proof that every conditional branch was visible
during the capture window.

The 2026-08-24 representative-scene run then passed all 27 catalog scenes in
54 isolated discovery/strict processes:

| Metric | Result |
| --- | ---: |
| Recipes/scenes passed | 27/27 |
| Strict native draws | 470,104 |
| Strict AOT shader misses | 0 |
| Compiled shader modules | 265 |
| Pack size | 2,249,168 bytes |
| Declared actor placements | 2,138 |
| Declared light records | 1,335 |
| Declared cutscene sources | 101 |

The aggregate evidence is
`I:\oot3dre_work\structural-scenarios\scene-matrices-proctex\20260824T155618Z\structural_scenario_matrix.json`.
Death Mountain Crater (`spot17_info_entry_0147`) exercises the newly closed
native ProcTex path; its strict pass submitted 15,384 draws with zero misses.

The complete entrance campaign passed all 82 recipes on the catalog v2 and
current runtime:

| Metric | Result |
| --- | ---: |
| Recipes/scenes passed | 82/82 across 27 scenes |
| Strict native draws | 1,416,671 |
| Refreshes containing native draws | 17,358 |
| Strict AOT shader resolutions | 7,263 |
| Strict AOT shader misses | 0 |
| Compiled shader modules | 303 |
| Pack size | 2,549,168 bytes |
| Declared actor placements | 6,561 |
| Declared light records | 4,737 |
| Declared cutscene sources | 358 |

The strict aggregate report is
`I:\oot3dre_work\structural-scenarios\all-entrances-v2\strict-matrix\20260824T171322Z\structural_scenario_matrix.json`.
The two lifecycle variants are evidence-visible rather than scene-specific
exceptions:

- `spot10_info_entry_05e0` observes requested entrance `0x05E0`, then the
  native loader resolves it to `0x011E` before completing all capture frames;
- `spot09_info_entry_01a5` remains stable through settle and 139 capture
  frames, then native gameplay requests departure `0x0219`.

## Coverage limits and next expansion

An entrance recipe proves that the original loader can construct and render a
specific scene under the anchor save context. It does not by itself cover every
age, day/night state, quest flag, room transition, cutscene branch or actor
state in that scene.

The first two expansion levels, one representative entrance for every catalog
scene and all 82 evidence-qualified entrances, are complete. Continue
structurally in this order:

1. generate a small versioned set of save-context profiles from decomp-proven
   setup predicates, then cross those profiles only with affected scenes;
2. add room/transition traversal recipes from decoded exits and transition
   actors;
3. add cutscene entry recipes from native cutscene references and orchestration
   tables;
4. promote newly observed shader modules only after discovery plus strict
   replay succeeds.

Manual gameplay remains useful for subjective playability and long stateful
flows, but it is no longer the primary mechanism for structural renderer
coverage.
