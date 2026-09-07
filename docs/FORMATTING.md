# Formatting

Project-owned C/C++ uses clang-format 14. The formatting scope is
`tools/oot3d` and `runtime/three_ds_recomp`; generated, third-party and external
sources are excluded.

```bash
./run-clang-format.sh
```

Override the executable when necessary:

```bash
CLANG_FORMAT=/path/to/clang-format-14 ./run-clang-format.sh
```

On Windows:

```powershell
.\run-clang-format.ps1 -ClangFormat C:\path\to\clang-format.exe
```

The optional `.pre-commit-config.yaml` applies the same ownership boundary to
staged source files.
