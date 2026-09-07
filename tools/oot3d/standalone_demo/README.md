# OOT3D Standalone Data PoC

This scaffold is a minimal data proof of concept outside Shipwright's runtime
replacement path. It proves that selected native or offline-derived OOT3D
resources can be extracted, packaged, inspected, and sanity-checked together.

It is not the correct OOT3D gameplay demo. The viewer's movement, collision
response, camera, and animation playback are simplified diagnostics and do not
represent native Link movement in OOT3D.

It uses:

- Link's House room visual mesh from OOT3D `scene/link_0_info.zsi`.
- Link's House native collision from OOT3D `scene/link_info.zsi`.
- Link child mesh and `child/anim/nml_run_free.csab` from the offline character
  conversion manifest.

Generated assets stay under `I:\oot3dre_work\standalone_demo\link_house` by
default and must not be committed.

Run from the repository root:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify
```

The gate writes:

```text
I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json
I:\oot3dre_work\standalone_demo\link_house\demo_verify_summary.json
I:\oot3dre_work\standalone_demo\link_house\simulation\movement_trace.json
I:\oot3dre_work\standalone_demo\link_house\preview\demo_preview.ppm
I:\oot3dre_work\standalone_demo\link_house\preview\demo_preview.png
I:\oot3dre_work\standalone_demo\link_house\viewer\viewer_self_test.json
I:\oot3dre_work\standalone_demo\link_house\native_demo_readiness.json
I:\oot3dre_work\standalone_demo\link_house\contracts\native_resource_contract.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace_compare.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_preview.png
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_viewer_self_test.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_resource_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_trace.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_preview.ppm
```

`demo_verify_summary.json` validates the data PoC only. The native-demo
readiness artifact is expected to stay `incomplete` until there is a dedicated
Shipwright/runtime/three_ds_recomp native launch path, an engine-hosted player update
path, an OOT3D reference input trace, and a trace comparison gate.
The native resource contract also declares the required runtime/three_ds_recomp resource
set for the first Link's House probe: room visual mesh, scene collision, Link
child model/animation, and player asset validation metadata.

To run the interactive PC viewer after the gate:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify -LaunchViewer
```

To print the native-readiness summary:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify -NativeReadiness
```

To regenerate only the native resource contract:

```powershell
python tools\oot3d\standalone_demo\oot3d_demo.py resource-contract --manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json
```

To run the separate native-host probe before printing readiness:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify -NativeHost -NativeReadiness
```

That command also compiles and runs the C++ runtime/three_ds_recomp resource probe through
`Invoke-Oot3dNativeResourceProbe.ps1`. The probe validates the native demo
resource contract and parses the native ZSI embedded CMB room mesh plus
ZAR/CMB/CSAB Link child sources before checking the derived runtime artifacts.
It also compiles and runs `Invoke-Oot3dNativeRuntimeProbe.ps1`, which writes a
C++ deterministic movement/collision/camera trace and a small collision/path
preview artifact. This is still a runtime probe, not native OOT3D movement
parity.

To require comparison against a captured OOT3D reference trace:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify -NativeHost -ReferenceTrace I:\path\to\oot3d_reference_trace.json -RequireReferenceTrace -NativeReadiness
```

To launch the native-host interactive viewer:

```powershell
.\scripts\oot3d\Invoke-Oot3dStandaloneDemo.ps1 -Verify -NativeHost -LaunchNativeHostViewer
```

The viewer is a minimal dependency-free Tkinter wireframe renderer. It loads the
generated room GLB, native ZSI collision XML, and Link child CSAB-derived morph
GLB. Use WASD or arrow keys to move Link with a simple follow camera. Treat any
movement or animation behavior here as diagnostic only.

The selected engine target for real iteration is documented in
`docs/OOT3D_RUNTIME_DEPENDENCY_CLEANUP.md`.
