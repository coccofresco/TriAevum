# OOT3D Decomp Support Bundle

This directory vendors the subset of the local OOT3D decomp workspace that is
useful for the Shipwright OOT3D port.

It is not a full game asset dump and must not grow into one. Keep extracted
RomFS files, generated resources, emulator traces, build outputs, and raw game
images outside this repository.

Primary contents:

- `docs`: local reverse-engineering and N64 porting workflow notes.
- `ghidra_scripts`: export and symbol-application helpers.
- `include/oot3d`: struct layout and semantic helper headers used by recovered
  source.
- `metadata`, `symbols`, and selected `analysis`: port maps, readiness reports,
  player collision packets, and function rankings.
- `scripts`: audit, export, ranking, and porting helpers.
- `src`: recovered source fragments that are currently useful to understand
  OOT3D behavior relevant to Shipwright asset acceptance.

The original source workspace remains at:

```text
E:\ppssppvr\oot3d_decomp\oot3d_decomp_git
```
