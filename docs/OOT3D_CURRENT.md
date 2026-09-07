# OoT3D Recomp Current Context

## Product Boundary

The product executable is `oot3d_native_game`. It consumes native OoT3D game
data through the runtime and asset contracts under `tools/oot3d` and
`runtime/three_ds_recomp`.

The repository no longer builds or embeds the Ship of Harkinian game, ZAPD, or
OTRExporter. The retained runtime descends from libultraship; that derivation is
recorded in `THIRD_PARTY_NOTICES.md`, while active project names are neutral.

## Runtime Layout

- `runtime/three_ds_recomp`: reusable 3DS recomp runtime and renderer module.
- `runtime/three_ds_recomp/include/three_ds_recomp/oot3d`: OoT3D adapter API.
- `runtime/three_ds_recomp/src/oot3d`: OoT3D adapter implementation.
- `tools/oot3d/native_game_runtime`: product host and game integration.
- `tools/oot3d/native_pica_frontend`: native PICA command ingestion.
- `tools/oot3d/oot3d_asset_tool`: offline native asset tooling.
- `tools/oot3d/renderer_integration`: dependency-boundary audit and policy.

The generic `Ship::` surface still used inside the retained runtime is an
explicit legacy compatibility layer. New code must use neutral interfaces; its
allowed surface is ratcheted by `renderer_dependency_policy.json`.

## Build And Run

```powershell
.\scripts\oot3d\Configure-Oot3dWholeAotProduct.ps1 -Force
.\scripts\oot3d\Build-Oot3dWholeAotProduct.ps1
.\scripts\oot3d\Invoke-Oot3dNativeGame.ps1
```

Extracted game data, builds, captures, and savestates are local inputs and must
not be committed.
