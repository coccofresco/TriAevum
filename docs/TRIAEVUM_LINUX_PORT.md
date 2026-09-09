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
available. This is not Wine or WSL. Native graphical boot and title animation
have now been verified through NRI framebuffer captures on this GPU.

First runtime build completed successfully: `~/triaevum-linux-build/TriAevum`
and `oot3d_game_module.so` are native Linux ELF binaries. `TriAevum --product-info`
exits 0 and reports NRI/F1/TopScreen enabled, ABI 2, default UI `topscreen`, and
`private_title_loaded=false` without a selected title. Explicit validation
correctly rejects the empty `triaevum_title_aot.so` stub and accepts the actual
Linux title with `private_title_loaded=true` and ABI 2.
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
2. The verified translated title now compiles into a Linux module and passes
   runtime ABI validation. Extend verification into interactive gameplay.
3. Forge now supports ELF modules, Linux paths and explicit target triples;
   its WSLg GUI and physical Linux ROM-to-installed-game test pass. On the
   physical KDE session the Forge window remains iconified during remote
   probes, so GUI visibility and release packaging are still open; see
   [Linux Forge](TRIAEVUM_LINUX_FORGE.md). Never compile on the player path.
4. Boot/title framebuffer verification is complete. Manually verify F1,
   TopScreen controls, audible output and a playable save on Linux.
5. Recover SSSR and check optional-provider capability reporting. Benchmark
   native and interpolated frame rates separately on the physical GPU.
6. Produce a clean-install Linux package and correct source-package omissions.

The 2026-09-09 Forge qualification installed a personal ROM in 25.55 seconds,
reinstalled in 7.62 seconds preserving config/save-sentinel hashes, and booted
the generated profile for 60 seconds with native framebuffer captures. No
player-side compilation. Linux RUNPATH now resolves the runtime's empty
bootstrap `.so` beside the executable after relocation; the real translated
title remains a separately selected immutable plugin. This is a private
candidate using host libraries, not yet a Steam-compatible portable package.

Never include personal ROMs, extracted assets, savestates or private inputs in
Git or public artifacts. Service test success is not evidence of a game boot.

## Publisher Title Build

`tools/triaevum_release/linux_title` builds the already-published translated
source, not a new translation. Configure-time SHA-256 checks validate every
declared source/header in `TITLE_SOURCE_MANIFEST.json`. Clang uses strict
floating point; hidden ELF symbols prevent internal title functions from
interposing on the host's identically named helpers. Only ABI query exports
are public title entry points. The linker rejects unresolved symbols.

```sh
cmake -S tools/triaevum_release/linux_title -B ../triaevum-linux-title-build \
  -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/triaevum-linux-deps/prefix" \
  -DTRIAEVUM_TRANSLATED_TITLE_DIR="$HOME/triaevum-linux-deps/title-source"
cmake --build ../triaevum-linux-title-build --parallel 3
```

This publisher build intentionally remains outside end-user Forge. Keep the
incremental objects. Do not replace the stub in the runtime build tree: select
the real title explicitly with `--title-plugin`.

The bounded native desktop test reuses a separate copy of a prepared personal
installation (relative data paths and configuration), overriding only the title
library. It imports display routing from the logged-in user's desktop session:

```sh
bash scripts/run-linux-title.sh "$HOME/triaevum-linux-play" \
  "$HOME/triaevum-linux-build" "$HOME/triaevum-linux-title-build" 60
```

Captures and logs are under that private installation's `linux-captures`.
This is a developer bring-up workflow, not the final Linux Forge package.

## Native Boot Evidence (2026-09-08)

Title module: `~/triaevum-linux-title-build/triaevum_title_aot.so`, 85,548,248 bytes,
SHA-256 `b305d574f815f365d7a73a582c020cd435f7ab2741accfa9638dced833a2097c`.
All 12,419 published functions were compiled from the unchanged translated
source manifest (256 shards plus registry). No new decompilation or translation.

- First run: normal exit after 45 seconds; framebuffer samples at 120, 420,
  720 show Hyrule Field, moon/sky and Link riding Epona. Native rendering plus
  grass and toon are visible. Peak frame interval: 20.764 seconds.
- Second run: normal exit after 80 seconds, with the 198-module PICA pack enabled.
  Later captures show the full title/logo and subsequent animated shots. The
  pack resolved 52 shader requests; 86 missed and used the normal runtime
  compiler (extensions alter effective shader variants). Peak interval remained
  14.341 seconds. AOT-pack selection alone does not solve graphics stalls.
- Audio diagnostics: DSP enabled, host output enabled and initialized, nonzero
  PCM samples at 32,728 Hz. This verifies the output path, not listening quality.
- Default configuration retained TopScreen, 2x visual interpolation, 1.10 FOV,
  toon and grass. Save data and configuration were copied to a separate test
  installation; the Windows originals were not changed.
- Four additional profile-preparation tests pass: preserve original profile,
  reject duplicate/missing plugin arguments, reject missing library.

Private Windows evidence copies:
`I:/oot3dre_work/linux-port-proof/linux-captures` (first run), and
`I:/oot3dre_work/linux-port-proof/second-run` (second summary, log, late capture).
Latest complete captures remain in the Linux test installation.

Performance is **not accepted yet**: these are paced, vsync-enabled capture runs
with cold shader/pipeline work and interpolation. Their aggregate presentation
rates (18.4 and 35.0 FPS) are not native simulation throughput measurements.
Guest execution took 2.07/9.34 seconds versus 35.76/49.44 seconds in the graphics
backend across the two runs. Profile shader/pipeline creation and steady-state
rendering before changing title execution or claiming a 60 FPS result.
