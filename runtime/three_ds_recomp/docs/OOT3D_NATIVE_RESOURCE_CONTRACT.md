# OoT3D Native Resource Contract

The OoT3D resource boundary is exposed as `ThreeDsRecomp::Oot3d` under
`include/three_ds_recomp/oot3d`. It accepts native OoT3D resources or data
directly derived from them and rejects runtime substitution with N64 assets.

`oot3d_native_resource_contract_v1` requires:

- `runtime_n64_asset_substitution_allowed` to be `false`;
- `shipwright_runtime_replacement_allowed` to be `false`, as a compatibility
  check for old manifests;
- every resource to be marked `native_or_directly_derived`;
- every `runtime_n64_asset_path` to be empty;
- runtime resource names to use the `oot3d.*` namespace.

The retained legacy field names are serialized compatibility keys, not runtime
dependencies. New code must use the native resource providers and must not add
replacement-path behavior.

Build the runtime through the product checkout:

```powershell
.\tools\oot3d\build_fast_dev.ps1 -Target three_ds_recomp_runtime
```

Set `THREE_DS_RECOMP_BUILD_TESTS=ON` to build the resource-contract and renderer
tests. The complete application contract is documented in the root
`docs/OOT3D_RUNTIME_DEPENDENCY_CLEANUP.md`.
