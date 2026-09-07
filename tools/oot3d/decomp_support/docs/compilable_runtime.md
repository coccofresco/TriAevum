# Compilable Runtime Reconstruction

The maintained source under `src/` is the path toward code that can be built,
disassembled, and iterated toward target assembly. The Ghidra C export remains
analysis input; it is not treated as bulk compilable source.

## Toolchain

Use devkitPro/devkitARM or another toolchain that provides `arm-none-eabi-gcc`
and `arm-none-eabi-objdump`.

On this Windows workspace the build has also been verified with MSYS2:

```powershell
C:\msys64\usr\bin\pacman.exe -S --needed --noconfirm mingw-w64-x86_64-arm-none-eabi-gcc mingw-w64-x86_64-arm-none-eabi-binutils mingw-w64-x86_64-arm-none-eabi-newlib
```

`build-runtime-objects.ps1` accepts `-ToolRoot` for an explicit ARM GNU
installation and otherwise searches `DEVKITARM`, `C:\devkitPro\devkitARM\bin`,
`PATH`, `C:\msys64\mingw64\bin`, `C:\msys64\ucrt64\bin`, and
`C:\msys64\clang64\bin`.

The local Keil MDK tools under `C:\Keil_v5\ARM` are not a target-compatible
replacement for this lane as currently licensed. `ARMCC 5.06u7` and
`ARMCLANG 6.21` both reject ARMv6/ARM11/MPCore; see
`analysis/keil_armcc_system_probe.md`.

The baseline 3DS code generation flags mirror devkitPro 3DS examples:

```text
-march=armv6k -mtune=mpcore -mfpu=vfp -mfloat-abi=hard -mtp=soft -mword-relocations
```

`-mfpu=vfp` is included for generic `arm-none-eabi-gcc` builds, where
hard-float is otherwise rejected for `armv6k`. devkitARM templates often omit
it because their toolchain configuration supplies an appropriate default.

The object build is intentionally link-free, so unresolved runtime/game symbols
are allowed while individual functions are reconstructed.

Use the N64 batch-porting planner before growing maintained source beyond the
current runtime seed:

```powershell
.\scripts\refresh-n64-porting-plan.ps1
```

Work one N64 source-file lane at a time. Port shared structs, macros, enum
names, and helper roles first; add only the lane's small `match-seed` functions
to the object build after they compile. Large `fanout-review` rows are
subsystem-reconstruction evidence, not direct source-copy candidates. This
refresh uses manual-symbol overlay and does not require a Ghidra re-export.

## Build

```powershell
.\scripts\build-runtime-objects.ps1
```

Example with the local Arm GNU 15.2 package:

```powershell
.\scripts\build-runtime-objects.ps1 -ToolRoot C:\Users\xander\Downloads\arm-gnu-toolchain-15.2.rel1-mingw-w64-x86_64-arm-none-eabi
```

Outputs are written under `build/runtime/`:

- `*.o`: compiled object files for maintained source.
- `*.dump`: `objdump -dr` disassembly.
- `manifest.json`: compiler, objdump, flags, and sources used.
- `compare_runtime_objects.{json,md}`: first-pass comparison against
  `ghidra_export/disassembly.txt`.

Current maintained runtime status: 44 functions are built and all 44 compare as
exact normalized instruction matches.

If the toolchain is not on `PATH`, pass `-ToolRoot`, set `DEVKITARM` to the
devkitARM directory, or install devkitPro so the tools exist under
`C:\devkitPro\devkitARM\bin`.

## Iteration Loop

1. Pick a small named function from `symbols/manual_symbols.csv` or the runtime
   trace summaries.
2. Extract its target material:

   ```powershell
   python .\scripts\extract_target_function.py --name oot3d_copy_u32x12_if_distinct
   ```

3. Implement or refine it in maintained source, keeping generated Ghidra output
   as analysis only.
4. Re-run `.\scripts\build-runtime-objects.ps1`.
5. Inspect `build/runtime/compare_runtime_objects.md`.
6. If the function is close, use a focused diff tool such as `objdiff` or a
   decomp.me scratch to iterate flags and source structure.

`extract_target_function.py` writes per-function target assembly, scratch
assembly, Ghidra pseudocode, and a small metadata file under
`analysis/target_functions/`.

At this stage the comparison script reports normalized instruction-text
equality and the first differing instruction. It normalizes common objdump/Ghidra
presentation differences such as aliases, branch targets, literal loads, and
register ranges. It does not prove binary relink equivalence, so near matches
and larger functions still need manual review.
