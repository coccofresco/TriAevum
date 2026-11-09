# OOT3D native application host

This directory began as the launch path of the pre-AOT native demo (Fast3D
demo, cutscene player, mesh viewer, resource and runtime probes, the Python
native-host probe over the standalone data PoC). That demo, its executables
and its Python tooling were removed in 2026-10; the game runs the translated
whole-AOT process only.

What remains:

| Path | Role |
|---|---|
| `oot3d_demo_host_{context,io,screenshot,window_timing}.{h,cpp}`, `oot3d_demo_host_types.h` | The application host the game links (`oot3d_native_application_host`): window, renderer, audio, framebuffer capture, frame timing and the host launch arguments (`Args`). |
| `input_timelines/` | Deterministic input timelines for qualification runs (for example `kokiri_renderer_parity.json`, used by `scripts/oot3d/Invoke-Oot3dNativeGameRendererParity.ps1`). Diagnostic input only; never consulted by an interactive run or used as a source of gameplay values. |
| `lighting_tables/oot3d_pica_lighting_semantics.json` | Lighting semantics read by the asset tool's playable pack (`oot3d_asset_tool/playable_pack.py`). |
| `oot3d_pica_trace_import.py` | Normalizes an Azahar PICA command-list trace into JSON (`scripts/oot3d/Convert-Oot3dNativePicaTrace.ps1`). |
| `oot3d_pica_command_list_watchlist.py` | Builds Azahar memory-watch ranges from a PICA frame JSONL dump (`scripts/oot3d/Invoke-Oot3dKokiriSlot5EmulatorDump.ps1`). |
| `oot3d_pica_light_origin_probe.py`, `oot3d_pica_native_origin_audit.py` | Diagnostic audits that trace observed PICA state back to native asset or code.bin evidence. The emulator trace is a query source only. |

## Deterministic renderer parity

The game accepts a test-only input timeline together with a fixed time step;
interactive runs keep keyboard input and wall-clock timing. The Kokiri parity
runner drives both renderers through the same updates, captures each
framebuffer through the rendering API, compares runtime checkpoints and applies
an RMSE threshold to rasterization differences:

```powershell
.\scripts\oot3d\Invoke-Oot3dNativeGameRendererParity.ps1
```

## Azahar PICA writer trace

For trace-backed provenance of vertex-shader uniform uploads, generate Azahar
memory-watch ranges from a frame JSONL dump:

```powershell
python .\tools\oot3d\native_demo_host\oot3d_pica_command_list_watchlist.py `
  --input <local>/oot3dre\captures\azahar_pica\oot3d_pica_frame_000000.jsonl `
  --output <local>/oot3dre\captures\azahar_pica\derived\watch_addresses.txt `
  --summary-out <local>/oot3dre\captures\azahar_pica\derived\watch_addresses.summary.json
```

Use the watchlist with the dedicated Azahar Dynarmic writer hook, not the
generic memory watchpoints:

```powershell
$env:OOT3D_PICA_WRITER_TRACE='1'
$env:OOT3D_PICA_WRITER_TRACE_WATCHLIST='<local>/oot3dre\captures\azahar_pica\derived\watch_addresses.txt'
$env:OOT3D_PICA_WRITER_TRACE_OUTPUT='<local>/oot3dre\captures\azahar_pica\derived\oot3d_pica_writer_trace.csv'
$env:OOT3D_PICA_WRITER_TRACE_MAX_ROWS='50000'
```

The resulting CSV gives the guest PC/LR/SP/R0-R12 context of the CPU writes
that touch those command-list words.
