# Runtime dependency cleanup

## Active boundary

The native product configures only the OoT3D runtime, source/AOT integration
and reusable 3DS renderer services. It does not configure or link the former
OoT N64 game, ZAPDTR, OTRExporter, N64 OTR generation or SoH packaging.

## Name map

| Former build identity | Active identity |
| --- | --- |
| root project `Ship` | `Oot3dRecomp` |
| submodule path `libultraship` | `runtime/three_ds_recomp` |
| CMake target `libultraship` | `three_ds_recomp_runtime` |
| CMake aliases `LibUltraShip::*` | `ThreeDsRecomp::*` |
| CMake options `LUS_*` | `THREE_DS_RECOMP_*` |
| iterative option `SOH_FAST_DEV_LINK` | `OOT3D_FAST_DEV_LINK` |

No compatibility CMake aliases are retained: stale consumers must fail at
configuration instead of silently restoring the obsolete dependency boundary.

## Retained compatibility surface

The runtime still contains `Ship::` platform APIs and a Fast3D adapter used by
the native application host. They are retained code, not dependencies on the
removed SoH executable. Their measured consumers are migrated component by
component to neutral host-service and PICA contracts; blind namespace-only
renaming is prohibited because it would conceal rather than remove coupling.

`tools/oot3d/renderer_integration/audit_renderer_dependencies.py` remains the
ratchet for this boundary. New gameplay, renderer and input modules must not
add `Ship::`, N64 or Fast3D dependencies.

## Attribution

Historical derivation belongs in `THIRD_PARTY_NOTICES.md`, retained licenses
and Git history, not in active product, target or package names.
