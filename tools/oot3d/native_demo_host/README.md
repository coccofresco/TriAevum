# OOT3D Native Demo Host Probe

This host is a first autonomous launch path for the OOT3D Link's House demo
assets. It is separate from Shipwright/N64 runtime replacement and consumes the
standalone demo manifest directly.

It is still a probe, not the final runtime/three_ds_recomp-native demo. The output records
that limitation and keeps the remaining promotion blockers explicit.

Run after the standalone data PoC has generated its manifest:

```powershell
python tools\oot3d\native_demo_host\oot3d_native_host.py --manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json
```

To launch the interactive native-host probe:

```powershell
python tools\oot3d\native_demo_host\oot3d_native_host.py --manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json --launch-viewer
```

Outputs:

```text
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace_compare.json
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_preview.png
I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_viewer_self_test.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_resource_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_probe.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_trace.json
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_runtime_preview.ppm
I:\oot3dre_work\standalone_demo\link_house\native_host\runtime/three_ds_recomp_native_mesh_viewer.json
```

The trace comparator is available separately:

```powershell
python tools\oot3d\native_demo_host\oot3d_trace_compare.py --candidate I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace.json --reference I:\oot3dre_work\standalone_demo\link_house\native_host\oot3d_reference_trace.json --output I:\oot3dre_work\standalone_demo\link_house\native_host\native_host_trace_compare.json
```

Without a reference trace, the comparison artifact reports `missing_reference`
while still proving that the gate is available.

The generated standalone manifest also writes:

```text
I:\oot3dre_work\standalone_demo\link_house\contracts\native_resource_contract.json
```

The dedicated runtime/three_ds_recomp fork seed validates that contract through
`ThreeDsRecomp::Oot3d` before native loaders are promoted. It also resolves the minimum
native demo resource set by role so the future loader can request OOT3D-native
resources without using N64/OTR replacement paths.

The C++ file probe can be run directly:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeResourceProbe.ps1 -Verify
```

It compiles `tools/oot3d/native_demo_host/oot3d_native_resource_probe.cpp`
against the `ThreeDsRecomp::Oot3d` implementation and writes native source parse counts
for ZSI embedded CMB room meshes plus ZAR/CMB/CSAB character sources.

The C++ runtime probe can be run directly:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeRuntimeProbe.ps1 -Verify
```

It consumes the standalone manifest, validates the native resource contract,
loads GLB metadata, parses the OOT3D-derived collision XML, emits a deterministic
movement trace, and writes a small preview artifact. It is still a standalone C++ probe
pending promotion into a windowed runtime/three_ds_recomp host.

The first textured windowed C++ visual viewer can be built and self-tested directly:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeMeshViewer.ps1 -Verify
```

Launch it interactively with:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeMeshViewer.ps1 -Launch
```

This viewer loads the room from the ZSI embedded CMB and child Link from the
ZAR/CMB source, uploads decoded native CMB textures, and samples frame 0 of
`boy/anim/nml_wait_free.csab` for Link's standing pose. Link is scaled from CMB
native character units into scene units using the manifest player height. It
intentionally has no collision or movement parity claim, and it does not use
runtime GLB meshes or N64/Shipwright replacement paths.

## PICA Shadow Trace Import

The native Fast3D demo can consume an explicit PICA register trace through the
manifest key `sources.native_pica_register_trace.path`. The trace is an offline
capture and normalization step, not a runtime N64/Shipwright asset substitution.

To collect the first useful shadow trace in Azahar:

1. Open the Graphics/Pica Command List debugger.
2. Click `Start Tracing`.
3. Enter Link's house or advance to the target Link's-house frame.
4. Click `Finish Tracing`, then `Copy All`.
5. Paste the copied table into a text file, for example:

```text
I:\oot3dre_work\standalone_demo\link_house\native_host\azahar_pica_commands.tsv
```

Normalize that table and create a derived manifest:

```powershell
.\scripts\oot3d\Convert-Oot3dNativePicaTrace.ps1 `
  -InputPath I:\oot3dre_work\standalone_demo\link_house\native_host\azahar_pica_commands.tsv `
  -OutputPath I:\oot3dre_work\standalone_demo\link_house\native_host\oot3d_native_pica_register_trace.json `
  -Manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json `
  -ManifestOut I:\oot3dre_work\standalone_demo\link_house\demo_manifest_with_pica_trace.json `
  -RequireShadowRegisters
```

Then run the demo against the derived manifest:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeFast3dDemo.ps1 `
  -Manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest_with_pica_trace.json `
  -Frames 1 -MaxSeconds 8
```

## Intro Cutscene Runtime Camera

The native Fast3D demo can also drive its render camera from the decompiled
intro cutscene runtime tables compiled into the executable. This uses the
native OOT3D QDB/CMAD/STRT/misc-action data under `tools/oot3d/decomp_support`;
it is not an emulator dump or N64 runtime substitution.

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeFast3dDemo.ps1 `
  -Manifest I:\oot3dre_work\standalone_demo\link_house\demo_manifest.json `
  -Launch -Frames 120 -MaxSeconds 8 -ExtraArgs @('--intro-cutscene-source-index','27')
```

The summary JSON records `intro_cutscene_runtime`, including the native
cutscene source row, active camera timeline row, frame, view, and misc-action
requests used by the demo camera.

When the capture includes `GPUREG_TEXUNIT0_SHADOW` and `GPUREG_FRAGOP_SHADOW`,
the demo JSON records the decoded native Shadow2D compare/generation bias values
under `engine_render_scene.pica_shadow`. The remaining native-shadow blocker
then moves to the real shader/material route instead of register import.

For trace-backed provenance of the Link vertex/hemisphere lighting uploads,
generate Azahar memory-watch ranges from a frame JSONL dump:

```powershell
python .\tools\oot3d\native_demo_host\oot3d_pica_command_list_watchlist.py `
  --input I:\oot3dre\captures\azahar_pica\oot3d_pica_frame_000000.jsonl `
  --output I:\oot3dre\captures\azahar_pica\derived\watch_addresses.txt `
  --summary-out I:\oot3dre\captures\azahar_pica\derived\watch_addresses.summary.json
```

The generated watchlist targets the virtual aliases for the command-list words
that upload VS f76..f86. Use it with the dedicated Azahar Dynarmic writer hook,
not the generic `pcvr_culling_probe` memory watchpoints:

```powershell
$env:OOT3D_PICA_WRITER_TRACE='1'
$env:OOT3D_PICA_WRITER_TRACE_WATCHLIST='I:\oot3dre\captures\azahar_pica\derived\watch_addresses.txt'
$env:OOT3D_PICA_WRITER_TRACE_OUTPUT='I:\oot3dre\captures\azahar_pica\derived\oot3d_pica_writer_trace.csv'
$env:OOT3D_PICA_WRITER_TRACE_MAX_ROWS='50000'
```

The resulting CSV gives the guest PC/LR/SP/R0-R12 context for CPU writes that
touch those command-list words, without forcing memory pages onto Azahar's
debug watchpoint path.

## Deterministic OpenGL/Vulkan parity

The native game host accepts a test-only input timeline together with a fixed
time step. Interactive runs still use the keyboard and wall-clock timing. The
Kokiri parity runner drives both renderers through the same movement,
animation, collision, actor, and camera updates, captures each framebuffer
directly through the rendering API, and compares lightweight runtime
checkpoints before applying an RMSE threshold to rasterization differences:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGameRendererParity.ps1
```

The default timeline is
`tools/oot3d/native_demo_host/input_timelines/kokiri_renderer_parity.json`.
It is diagnostic input only and is never consulted by an interactive game
run or used as a source of OOT3D gameplay values.

After capturing the same title-intro checkpoint matrix with
`Invoke-Oot3dTitleIntro.ps1` into `opengl` and `vulkan` subdirectories, verify
the semantic state and direct framebuffer output together with:

```powershell
.\scripts\oot3d\Compare-Oot3dTitleIntroRendererParity.ps1 `
  -CaptureRoot I:\oot3dre_work\vulkan\title_renderer_parity
```

The comparison excludes only `title_intro_runtime.performance`, whose values
are wall-clock CPU telemetry rather than renderer state.
