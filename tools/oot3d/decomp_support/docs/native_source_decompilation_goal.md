# Native Source Decompilation Goal

## Goal Prompt

Decompile progressively the source files and data definitions that produce OOT3D native assets, formats, runtime structures, and behavior by using code.bin, original assets, Ghidra exports, maintained source lanes, and emulator traces only as validation; use N64 source only as secondary context for names, semantic hypotheses, and gameplay patterns, and confirm every structure or behavior against OOT3D before promoting it, without runtime N64 adaptation or unproven approximation.

## Evidence Policy

- Primary evidence: OOT3D `code.bin`, Ghidra target disassembly/decompiles, native asset files, maintained OOT3D source lanes, generated target windows, compiled ARM object comparisons, and code.bin-backed analysis reports.
- Secondary evidence: zeldaret/OOT N64 source names, source lanes, constants, and gameplay patterns. These guide hypotheses but do not prove OOT3D source shape by themselves.
- Validation evidence: emulator traces, PICA dumps, screenshots, and runtime observations. These validate behavior and identify targets, but runtime data must still be traced back to OOT3D code or assets before engine/source promotion.

## Current Operational Queue

Regenerate the OOT3D-first source decompilation queue from current reports:

```powershell
python .\scripts\build_native_source_decompilation_queue.py
```

Primary output:

- `analysis/native_source_decompilation_queue.md`
- `analysis/native_source_decompilation_queue.csv`
- `analysis/native_source_decompilation_queue.json`

The first expected work unit is the smallest structured C lowering case currently proven by native OOT3D evidence: `00398484` `oot3d_boss_va_zapper_intro` in `src/overlays/actors/ovl_Boss_Va/z_boss_va.c`. Its next gate is a focused structured comparison:

```powershell
python .\scripts\structured_c_match_gate.py --unit boss_va_zapper
```

Promotion rule: only exact C rows from the structured gate can replace exact/inline anchors in the maintained baseline. Near rows stay as structured work until the remaining mismatch is explained by OOT3D evidence.
