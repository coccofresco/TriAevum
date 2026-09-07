# OoT3D Operational Inputs

Machine-local tools and original game inputs are declared in
`tools/oot3d/operational_inputs.json`. Validate them without modifying source
inputs:

```powershell
.\scripts\oot3d\Test-Oot3dOperationalInputs.ps1
```

Configure and build the current product with:

```powershell
.\scripts\oot3d\Configure-Oot3dWholeAotProduct.ps1 -Force
.\scripts\oot3d\Build-Oot3dWholeAotProduct.ps1
```

For a narrow iterative build:

```powershell
.\tools\oot3d\build_fast_dev.ps1 -Target oot3d_native_game
```

The build links `three_ds_recomp_runtime` from
`runtime/three_ds_recomp`. It does not require the removed OoT N64 game,
ZAPDTR, OTRExporter, OTR generation, or a second game checkout. Historical
evidence under `tools/oot3d/decomp_support` is read-only input and is not part
of the product dependency graph.

See `OOT3D_RUNTIME_DEPENDENCY_CLEANUP.md` for the retained compatibility
boundary and attribution policy.
