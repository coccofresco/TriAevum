# Emulator instrumentation plan

Static decompilation is enough to find function boundaries, helper routines, and likely subsystems, but it does not prove runtime meaning. The next productive step is to instrument an emulator and produce JSONL traces that can be joined against `analysis/callgraph.json` and `ghidra_export/functions.csv`.

## Target runtime

Use a 3DS emulator with source-level control. The active local target is Azahar at `..\azahar_instrumented`, with the OOT3D trace hook committed as `4314ccc` plus the Windows MinGW linker fix `8d889a1`. The first pass does not need full symbolic debugging. It needs deterministic trace points that log ARM11 userland PC values and selected registers.

The current Azahar hook sits in the DynCom interpreter path, so CPU JIT must be disabled before collecting trace data. See `docs/azahar_runtime_trace.md` for build and launch commands.

The trace format is one JSON object per line. Each line should validate against `schemas/runtime_trace_event.schema.json`.

Validate a trace with:

```powershell
python .\scripts\validate_runtime_trace.py .\traces\oot3d-first-run.jsonl
```

Minimal function event:

```json
{"event":"function_enter","pc":"0x00375bcc","function_entry":"0x00375bcc","function_name":"FUN_00375bcc","frame":120,"tick":730012,"thread_id":0,"args":["0x08000000","0x00000001","0x00000000","0x00000000"],"sp":"0x0ffff240","lr":"0x00250b48"}
```

## Initial breakpoints

Start with high fan-in functions from `analysis/callgraph_report.md`; they are likely shared helpers, dispatchers, or state accessors:

| Entry | Current name | Reason |
| --- | --- | --- |
| `0x00375bcc` | `FUN_00375bcc` | 696 callers |
| `0x003731e0` | `FUN_003731e0` | 653 callers |
| `0x003679b4` | `FUN_003679b4` | 613 callers |
| `0x0036ae14` | `FUN_0036ae14` | 547 callers |
| `0x002cfca0` | `oot3d_sin_idx8` | 507 callers |
| `0x00372224` | `oot3d_copy_u32x12_if_distinct` | known helper, 313 callers |

Then add high fan-out functions to capture subsystem boundaries:

| Entry | Current name | Reason |
| --- | --- | --- |
| `0x00250ad0` | `FUN_00250ad0` | 88 outgoing calls |
| `0x00416608` | `FUN_00416608` | 60 outgoing calls |
| `0x002e2e60` | `FUN_002e2e60` | 51 outgoing calls |
| `0x0044e2d0` | `FUN_0044e2d0` | 50 outgoing calls |
| `0x002c5ba0` | `FUN_002c5ba0` | 49 outgoing calls |

## Instrumentation points

1. Function entry and return for selected addresses. Log `pc`, `lr`, `sp`, `r0-r3`, frame counter, and thread id.
2. RomFS file open/read calls. Log `romfs_path`, size, return handle/result, and caller PC.
3. Heap allocation and free. Log requested size, returned pointer, and caller PC.
4. Scene transition writes. Log current scene id and any table pointer touched by scene loading code.
5. Actor lifecycle points. Log actor id, object pointer, update function pointer, draw function pointer, and caller PC.

## Join strategy

After collecting a trace:

1. Normalize PCs to lowercase `0x%08x`.
2. Resolve `function_entry` by exact match in `ghidra_export/functions.csv`.
3. Join dynamic caller/callee pairs against `analysis/callgraph.json`.
4. Promote names when a function has stable runtime evidence, for example `scene_load`, `actor_spawn`, or `romfs_open`.
5. Re-run `scripts/promote_manual_symbol.py`, `scripts/validate_manual_symbols.py`, and `scripts/ghidra-apply-manual-symbols.ps1 -Reexport`.

## Practical first experiment

Boot to the title screen, start a new file, enter Kokiri Forest, open the pause menu, then close the emulator. This should cover startup, RomFS reads, scene load, actor creation, frame update, draw, and menu code without requiring long gameplay.

Keep the first trace bounded to 60 seconds or less. High fan-in helpers may fire thousands of times per second, so the emulator hook should support an allowlist and per-address sampling.

The initial Azahar hook supports an allowlist through `OOT3D_TRACE_ALLOWLIST`, a global cap through `OOT3D_TRACE_MAX_EVENTS`, and per-address hit sampling through `OOT3D_TRACE_SAMPLE`.
