# ThreeDsRecomp Runtime Integration

`three_ds_recomp_runtime` is the reusable platform and rendering runtime used by
the OoT3D recompilation. Consumers should link the stable CMake alias:

```cmake
target_link_libraries(my_3ds_recomp PRIVATE ThreeDsRecomp::Runtime)
```

Title-specific native asset interpretation belongs under
`include/three_ds_recomp/<title>` and `src/<title>`. Reusable renderer,
presentation, input, audio, archive, and diagnostics services belong in their
generic runtime modules.

The inherited N64 compatibility surface is isolated under
`include/three_ds_recomp/legacy` and `src/legacy_compat`. New 3DS-native code
must not add dependencies on that surface. It remains only while measured
legacy consumers are migrated.

Useful configuration options are:

| Option | Default | Purpose |
| --- | --- | --- |
| `THREE_DS_RECOMP_ENABLE_VULKAN` | `ON` | Build the native 3DS Vulkan backend. |
| `THREE_DS_RECOMP_ENABLE_DX11` | `OFF` | Build the inherited Direct3D 11 backend. |
| `THREE_DS_RECOMP_FAST_DEV_LINK` | `OFF` | Reduce iterative linker output. |
| `THREE_DS_RECOMP_BUILD_TESTS` | `OFF` | Build runtime unit and renderer tests. |

Build and launch the complete product through the owning checkout scripts in
`scripts/oot3d` and `tools/oot3d`. The runtime's
`tools/Invoke-Oot3dRendererDev.ps1` uses that checkout directly and does not
clone or initialize a second game repository.

The upstream lineage and licenses are recorded in `THIRD_PARTY_NOTICES.md`.
