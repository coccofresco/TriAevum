# Native Linux Port

Development branch: `port/linux-nri`, based on public main
`55e0c40823949e0a341fa88e8bd5e298d7cc9e11`.

## Scope

Keep the verified game translation, NRI/Vulkan renderer, SDL controls/audio,
F1 and TopScreen. Adapt host/platform boundaries, not gameplay. Build the title
as an ELF shared library on the publisher's machine. End-user Forge must still
require only a supported personal decrypted ROM and no compiler or SDK.
Windows release behavior and artifacts are unchanged by this branch.

## Native Test Host

Verified on 2026-09-08: CachyOS x86_64, KDE Wayland, 12 logical CPUs, 15 GiB RAM,
NVIDIA RTX 4060 (8 GiB), proprietary driver 610.57.04. Vulkan enumerates the
physical GPU through SSH. Clang 22.1.8, CMake 4.4.2, Ninja, SDL2 and shaderc are
available. This is not Wine or WSL. No graphical game launch has been verified.

First runtime build completed successfully: `~/triaevum-linux-build/TriAevum`
and `oot3d_game_module.so` are native Linux ELF binaries. `TriAevum --product-info`
exits 0 and reports NRI/F1/TopScreen enabled, ABI 2, default UI `topscreen`, and
`private_title_loaded=false`. Explicit validation correctly rejects the empty
`triaevum_title_aot.so` stub. This is an executable host, not a playable title yet.
The PICA AOT shader pack contains 198 generated modules (schema 3).

Dedicated checkout and outputs on the test machine:

- `~/triaevum-linux`: source; existing repositories are untouched.
- `~/triaevum-linux-build`: incremental runtime build.
- `~/triaevum-module-build`: isolated service/package tests.
- `~/triaevum-linux-deps`: user-local dependencies; no system upgrade or sudo.

User-local dependencies recovered for this host:

- Vulkan-Headers v1.4.341, commit `b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2`.
- nlohmann/json v3.11.3, commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`.
- tinyxml2 10.0.0, commit `321ea883b7190d4e85cae5512a12e5eaa8f8731f`, PIC static build.

## Reproducible Runtime Build

Run explicitly with Bash (the test user's interactive shell is fish):

```sh
env TRIAEVUM_DEPS_PREFIX="$HOME/triaevum-linux-deps/prefix" \
    TRIAEVUM_VULKAN_INCLUDE="$HOME/triaevum-linux-deps/Vulkan-Headers/include" \
    TRIAEVUM_UI_EVIDENCE="$HOME/triaevum-linux-deps/ui-evidence" \
    bash scripts/build-linux-runtime.sh
```

The script limits compilation to three concurrent jobs and the two product
targets. Keep build trees between iterations. SSSR is explicitly disabled for
this initial bring-up: its pinned shader-generation path invokes
`FidelityFX_SC.exe`, and NRI does not populate that FFX dependency on Linux.
This is a temporary missing feature, not Linux feature parity. CACAO remains
enabled and its SPIR-V generation has completed using native Linux DXC.

## Changes and Evidence

- The explicit title plugin selector now uses `dlopen(RTLD_NOW | RTLD_LOCAL)`
  and `dlsym` on Linux, with existing ABI validation. The default module is
  resolved beside `/proc/self/exe`, not relative to the working directory.
  Keep the module loaded for the lifetime of its exported ABI function tables.
- Restrict NRI's `/WX-` adjustment to MSVC-compatible Clang; ordinary Linux
  Clang must not receive a Windows compiler argument.
- Link the optional development-only native math target only when it exists.
  The product runtime has no unresolved dependency on its helpers; previously
  the missing target was converted to a nonexistent `-loot3d_native_math`.
- Fix recursive JSON metadata storage to use a forward-declared vector element
  instead of instantiating `std::pair` with an incomplete recursive value type.
- All twelve native module test executables compile and exit successfully:
  TAM, metadata, input, audio, filesystem, registry, PICA adapter/client/scanout,
  guest memory leases, C ABI, and an actual ELF mock-module load/session.
  Repeat with `bash scripts/test-linux-module.sh`. Standalone CTest does not
  register these executables; the script runs them explicitly and fails on
  the first nonzero exit.
- The public source is missing the pinned `oot3d_ui` contract snapshot still
  referenced by CMake. The original files were recovered read-only from
  `I:/oot3dre_work/oot3d-native-renderer-integration/tools/oot3d/decomp_support/evidence/zelda3drecomp/849140697187b895/oot3d_ui`.
  The Linux copy is outside the public checkout; `OOT3D_NATIVE_UI_EVIDENCE_ROOT`
  now accepts an explicit external snapshot. This source-release completeness
  issue remains to be repaired with proper provenance, not hidden stubs.

## Remaining Acceptance Steps

1. Runtime build is complete; preserve the incremental tree for later changes.
2. Compile the verified translated title archive into a Linux module, with
   ABI/layout checks and deterministic floating-point settings.
3. Teach publisher catalog and Forge about ELF modules, Linux paths and target
   triples. Do not repurpose Windows binaries or compile on the player path.
4. Test real boot on the active Wayland desktop, capturing the framebuffer;
   then verify F1, TopScreen, audio, input and a playable save.
5. Recover SSSR and check optional-provider capability reporting. Benchmark
   native and interpolated frame rates separately on the physical GPU.
6. Produce a clean-install Linux package and correct source-package omissions.

Never include personal ROMs, extracted assets, savestates or private inputs in
Git or public artifacts. Service test success is not evidence of a game boot.
