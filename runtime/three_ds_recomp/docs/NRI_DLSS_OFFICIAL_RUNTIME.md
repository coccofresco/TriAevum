# Official NVIDIA DLSS Runtime

The NRI/Vulkan renderer integrates NVIDIA DLSS Super Resolution and DLAA through the official NVIDIA NGX SDK. The pinned SDK version is `310.7.0`.

This path does not load or support leaked, modified, or unsigned NVIDIA binaries. At configure and deployment time, the runtime must:

- have a valid Windows Authenticode signature;
- be signed by NVIDIA Corporation;
- match `THREE_DS_RECOMP_NGX_VERSION`.

The default runtime comes from the pinned official SDK. A separately provisioned official runtime can be selected with `THREE_DS_RECOMP_DLSS_RUNTIME`; it is subject to the same checks. The integration does not bypass NGX hardware or driver capability checks.

The current renderer path provides DLSS Super Resolution and DLAA. Frame Generation and Multi Frame Generation are separate features and are not part of this integration.

## Configuration

Configure the runtime with:

```cmake
-DTHREE_DS_RECOMP_NGX_VERSION=310.7.0
-DTHREE_DS_RECOMP_DLSS_RUNTIME=C:/path/to/official/nvngx_dlss.dll
```

Omit `THREE_DS_RECOMP_DLSS_RUNTIME` to use the runtime supplied by the pinned official SDK. In the F1 graphics menu, select `NVIDIA DLSS` and choose a Super Resolution quality mode or `DLAA (native resolution)`.

## Validation

Run the automated NRI/Vulkan gate after building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Test-Oot3dDlssUpscaler.ps1 -SkipBuild -BuildDirectory <build-directory> -Frames 120 -MaxSeconds 60
```

The gate verifies the deployed DLL identity, Vulkan/NGX capability discovery, scene-resolved DLSS dispatches, motion-vector and history inputs, NRI output ownership, barriers, and validation-layer errors.
