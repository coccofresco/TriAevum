# OOT3D Offline Semantic Source

## Purpose

The decompiled N64 game is used offline to reconstruct a stable gameplay control plane. It does not provide concrete OOT3D setup indices, actor entries, transforms, camera indices, animation clips, materials, or timing.

The authored source is:

```text
tools/oot3d/oot3d_asset_tool/profiles/semantic_gameplay_source.json
```

The semantic route compiler joins it with independently verified OOT3D bindings and native indexes:

```text
OOT N64 source and tables
    -> semantic_gameplay_source.json
    -> semantic route compiler
       + semantic_scene_entry_bindings.json
       + OOT3D code.bin global entrance table
       + OOT3D asset catalog
    -> oot3d_semantic_route_catalog.json
    -> oot3d-core.o2r
```

Only the generated route catalog is consumed at runtime. The N64 source is not reparsed and its numeric setup index is not used as an OOT3D identity.

## Source Contract

Each record identifies an intent, its N64 trigger, and one or more semantic variants:

```json
{
  "semantic_key": "SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE",
  "kind": "scene_entry",
  "trigger": {
    "scene_id": 85,
    "entrance_index": 529,
    "entrance_symbol": "ENTR_KOKIRI_FOREST_OUTSIDE_LINKS_HOUSE",
    "origin_semantic_key": "SCENE_LINKS_HOUSE_EXIT"
  },
  "variants": [
    {
      "variant_key": "KOKIRI_FOREST_INITIAL_CHILD_DAY",
      "conditions": [
        { "fact": "player.age", "equals": "child" },
        { "fact": "world.day_phase", "equals": "day" },
        { "fact": "gameplay.layer", "equals": "normal" }
      ]
    }
  ]
}
```

The current fact vocabulary is deliberately small and validated by the compiler. New facts must represent stable gameplay meaning, not an OOT or OOT3D memory address, setup number, actor parameter, or asset path.

## Runtime Contract

Ship emits the current semantic facts at an established gameplay boundary. `SemanticRouteCatalog` selects the most specific unambiguous variant and `NativeSceneEntryProvider` then reads the routed OOT3D ZSI.

For a scene entry, OOT3D owns:

- native scene and setup;
- global and local entrance;
- native room and player spawn, reached through an explicit scaffold-to-native room binding;
- position, rotation, player parameters, start mode, and camera selector;
- actor/object population and concrete actor configuration.

After the entry is applied, the bridge keeps the resolved native scene, setup, and room as a runtime context. Scene rendering, actor lighting, environment lighting, and fog consume those native coordinates instead of reusing the numerically similar Ship setup or room. The native player parameter is then handed to Ship's established camera bootstrap, which resolves it against the collision-camera records decoded from the routed OOT3D scene.

Ship continues to own the gameplay loop, save progression, HUD, menus, and shared behavior until a verified OOT3D delta replaces a specific behavior.

## Rejected Designs

- Directly treating an N64 `sceneSetupIndex` as the OOT3D setup index.
- Copying the full N64 source tree and editing numeric content references in place.
- Resolving semantic variants from hardcoded strings in `Play_Init`.
- Reading emulator dumps at runtime.
- Assuming that a Ship room number is also the OOT3D room number because the values happen to match.
- Replacing an OOT3D camera selector with a route-local or N64 camera value.

## Extension Process

1. Extract the trigger and variant meaning from N64 source into the offline semantic source.
2. Verify the matching OOT3D setup or native structure from ZSI, `code.bin`, or other original data.
3. Add a separate native binding with provenance, including every scaffold-to-native room association needed by the route.
4. Regenerate and verify the semantic route catalog.
5. Add a runtime fact only when the existing vocabulary cannot express the semantic condition.
6. Implement any missing native provider capability before activating a route that requires it.

## Commands

```powershell
.\scripts\oot3d\Invoke-Oot3dSemanticRouteCatalog.ps1 -Verify
.\scripts\oot3d\Invoke-Oot3dPlayablePack.ps1 -Verify
```

The generated catalog and package are written under:

```text
I:\oot3dre_work\playable_catalog\oot3d_semantic_route_catalog.json
I:\oot3dre_work\playable_pack\oot3d-core.o2r
```
