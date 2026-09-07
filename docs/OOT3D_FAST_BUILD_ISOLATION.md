# OoT3D fast build isolation

## Boundary

The native product is built from `oot3d_native_game`; the former monolithic
OoT N64 target is no longer configured or present. Libraries must keep private
include directories, definitions and compile options private so a local change
does not invalidate unrelated AOT, runtime or renderer objects.

`oot3d_room_compilation_runtime` is a representative enforced boundary: CMake
fails if its implementation include directory enters the public interface.

## Build guard

`tools/oot3d/build_fast_dev.ps1` refreshes and freezes the Ninja graph, inspects
the no-execute plan and runs exactly the inspected manifest. The native game
defaults to at most 48 compile actions and 160 total actions; other targets
default to 24 and 96. An intentional toolchain or path migration requires
`-AllowBroadRebuild`.

```powershell
.\tools\oot3d\build_fast_dev.ps1 `
  -Target oot3d_native_game `
  -Parallel 12
```

The guard is a fan-out detector, not a substitute for a clean build. A module
path or compiler migration legitimately invalidates objects once; subsequent
iterations must return to the narrow bounds.
