# Manual decompilation workflow

The repository keeps two layers separate:

- `ghidra_export/` is the generated baseline.
- `src/`, `include/`, and `symbols/manual_symbols.csv` are the maintained decompilation layer.

When a function is understood:

1. Add or update a maintained source implementation under `src/`.
2. Add the public/internal declaration under `include/` if useful.
3. Add an entry to `symbols/manual_symbols.csv` with the original Ghidra name, new name, confidence, source file, and notes.
4. Run:

```powershell
python .\scripts\validate_manual_symbols.py
```

The validator checks that every manual symbol points to an existing Ghidra function and source file. It accepts either the original Ghidra placeholder name or the applied manual name, so it can be run before or after `ApplyManualSymbols.java`.

For the common case, use the promotion helper instead of editing CSV manually:

```powershell
python .\scripts\promote_manual_symbol.py --entry 0036df4c --new-name oot3d_copy_u32x3 --source-file src/runtime/runtime_helpers.c --confidence high --notes "Copies three 32-bit words from src to dst."
```

For repetitive copy helpers, use the discovery report first:

```powershell
python .\scripts\discover_copy_helpers.py
```

This writes `analysis/copy_helper_candidates.md` and marks functions with extra writes or calls separately from pure copy helpers.

To push the names into the local Ghidra project:

```powershell
.\scripts\ghidra-apply-manual-symbols.ps1
```

To push the names and regenerate the exported pseudocode and function index:

```powershell
.\scripts\ghidra-apply-manual-symbols.ps1 -Reexport
python .\scripts\analyze_exports.py
```

Current manually named functions:

| Entry | Ghidra name | Manual name | Confidence | Notes |
| --- | --- | --- | --- | --- |
| `00100028` | `FUN_00100028` | `oot3d_clear_bss` | high | Startup BSS clear loop. |
| `0010004c` | `FUN_0010004c` | `oot3d_copy_sparse_words_36` | medium | Packs selected source words into a contiguous 36-byte destination. |
| `001000ec` | `FUN_001000ec` | `oot3d_strncmp` | medium | String compare with length limit and NUL termination behavior. |
| `002f48d4` | `FUN_002f48d4` | `oot3d_copy_u16x4` | high | Copies four 16-bit values. |
| `00304380` | `FUN_00304380` | `oot3d_copy_u8x5` | high | Copies five bytes. |
| `00324744` | `FUN_00324744` | `oot3d_copy_u32x16_if_distinct` | high | Copies sixteen 32-bit words unless source and destination match. |
| `0033ddbc` | `FUN_0033ddbc` | `oot3d_copy_u32x9` | high | Copies nine 32-bit words. |
| `0035fb94` | `FUN_0035fb94` | `oot3d_copy_u16x3` | high | Copies three 16-bit values. |
| `00363f20` | `FUN_00363f20` | `oot3d_copy_u8x4` | high | Copies four bytes. |
| `0036df4c` | `FUN_0036df4c` | `oot3d_copy_u32x3` | high | Copies three 32-bit words. |
| `00372224` | `FUN_00372224` | `oot3d_copy_u32x12_if_distinct` | high | Copies twelve 32-bit words unless source and destination match. |
