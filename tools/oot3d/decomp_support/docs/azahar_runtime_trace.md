# Azahar runtime trace workflow

The local instrumented emulator lives outside this repository:

- Azahar clone: `..\azahar_instrumented`
- Local trace commit: `4314ccc Add OOT3D interpreter trace hook`
- Windows linker fix commit: `8d889a1 Link httplib with crypt32 on Windows`
- Sampling commit: `72dc346 Add OOT3D trace sampling`
- Verified frontend build: `..\azahar_instrumented\build-oot3d-trace-mingw-nolto\bin\Release\azahar.exe`

The hook is intentionally opt-in. Azahar behaves normally unless `OOT3D_TRACE_PATH` is present in the environment.

## Build status

Two builds were verified on this machine:

```powershell
# Core-only MSVC build used to validate the trace source against citra_core.
cmd /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --build build-oot3d-trace-msvc2 --target citra_core --parallel 8"

# Full Qt frontend build used to produce azahar.exe.
C:\msys64\usr\bin\bash.exe -lc "export PATH=/mingw64/bin:/usr/bin:$PATH; cd /e/ppssppvr/oot3d_decomp/azahar_instrumented && cmake --build build-oot3d-trace-mingw-nolto --target citra_meta --parallel 8"
```

The full MinGW build must use `-DENABLE_LTO=OFF`. With LTO enabled, MinGW failed during final link in CryptoPP unrelated to the trace hook.

## Runtime requirements

The current hook is installed in the DynCom interpreter path. Disable CPU JIT in Azahar before collecting a trace, otherwise normal JIT execution will not pass through the trace hook.

Launch Azahar from PowerShell with MSYS2 Qt DLLs on `PATH`:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$env:OOT3D_TRACE_PATH = "E:\ppssppvr\oot3d_decomp\oot3d_decomp_git\traces\oot3d-first-run.jsonl"
$env:OOT3D_TRACE_MAX_EVENTS = "200000"
$env:OOT3D_TRACE_SAMPLE = "1"
$env:OOT3D_TRACE_ALLOWLIST = "0x00375bcc,0x003731e0,0x003679b4,0x0036ae14,0x002cfca0,0x00372224,0x00250ad0,0x00416608,0x002e2e60,0x0044e2d0,0x002c5ba0"
..\azahar_instrumented\build-oot3d-trace-mingw-nolto\bin\Release\azahar.exe
```

`OOT3D_TRACE_ALLOWLIST` accepts hex addresses separated by comma, semicolon, or whitespace. If it is omitted, the emulator uses the same initial high-value address set shown above.

`OOT3D_TRACE_SAMPLE` is a per-address sampling period. The default `1` emits every hit. `100` emits the first hit for each allowlisted PC and then one hit per 100 hits for that PC.

## Event format

The first hook emits `function_enter` JSONL events:

```json
{"event":"function_enter","pc":"0x00375bcc","function_entry":"0x00375bcc","function_name":"FUN_00375bcc","tick":1,"thread_id":0,"args":["0x08000000","0x00000001","0x00000000","0x00000000"],"sp":"0x0ffff240","lr":"0x00250b48"}
```

Fields:

- `pc` and `function_entry`: current interpreter PC when it matches the allowlist.
- `function_name`: known name for the built-in initial allowlist.
- `tick`: trace event counter, not an emulator global cycle count.
- `args`: `r0-r3` at function entry.
- `sp` and `lr`: stack pointer and link register at function entry.

Validate collected traces from this repository:

```powershell
python .\scripts\validate_runtime_trace.py .\traces\oot3d-first-run.jsonl
```

Resolve dynamic callers against the Ghidra export:

```powershell
python .\scripts\summarize_runtime_trace.py .\traces\oot3d-bootstrap-sample100.jsonl --markdown .\analysis\runtime_trace_sample100_summary.md --json .\analysis\runtime_trace_sample100_summary.json
```

## First collection pass

Use a short deterministic route:

1. Boot OOT3D to the title screen.
2. Start or load a file.
3. Enter Kokiri Forest.
4. Open and close the pause menu.
5. Close Azahar.

Then validate the JSONL and inspect which allowlisted functions fired. Promote names only after runtime evidence is stable and matches the static callgraph context.

## First capture result

A first bootstrap capture was collected on 2026-06-20 with 199990 validated `function_enter` events. A second capture without the copy helper collected 41314 validated events. A third capture with `OOT3D_TRACE_SAMPLE=100` collected 5568 validated events using the full allowlist. The summary is in `analysis/runtime_trace_bootstrap.md`.

The result shows `oot3d_copy_u32x12_if_distinct` consuming most of the event budget. For the next capture, use this narrower allowlist to expose less frequent subsystem functions:

```powershell
$env:OOT3D_TRACE_ALLOWLIST = "0x00375bcc,0x003731e0,0x003679b4,0x0036ae14,0x002cfca0,0x00250ad0,0x00416608,0x002e2e60,0x0044e2d0,0x002c5ba0"
```

After the second capture, `0x002cfca0` became the dominant target. The third pass validated `OOT3D_TRACE_SAMPLE=100`; this should be the default for short exploratory captures.

Resolved caller reports were generated for the three bootstrap captures:

- `analysis/runtime_trace_full_allowlist_summary.md`: 199990 events, 88 dynamic edges, 81 static callgraph matches, 0 unresolved LR sites.
- `analysis/runtime_trace_nocopy_summary.md`: 41314 events, 53 dynamic edges, 48 static callgraph matches, 0 unresolved LR sites.
- `analysis/runtime_trace_sample100_summary.md`: 5568 events, 60 dynamic edges, 52 static callgraph matches, 0 unresolved LR sites.
