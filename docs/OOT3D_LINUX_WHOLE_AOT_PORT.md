# OOT3D whole-AOT port for Linux

## Runtime boundary

The Linux product compiles the same 12,419 generated whole-AOT functions as
the Windows product into native x86-64 ELF objects. The product audit permits
three explicit host boundaries and zero residual A32 entries. The decoded A32
interpreter and the source-native reconstruction are not linked into the
gameplay closure; source-native remains documentation only.

Shared optimization evidence and the Windows adoption protocol are recorded in
the [cross-platform performance status](OOT3D_CROSS_PLATFORM_PERFORMANCE_STATUS.md).

The renderer selected at launch is the native 3DS Vulkan/PICA backend. NRI,
CACAO and SSSR are optional enhancement layers and are currently disabled, so
the base Vulkan path does not depend on NVIDIA or FidelityFX SDKs. The generic
desktop runtime still contains its OpenGL compatibility backend, but
`run-product-vulkan.sh` explicitly selects Vulkan.

## Build

The current local dependency staging lives in `build-linux-deps`: Vulkan
headers, nlohmann-json and tinyxml2. SDL2, Vulkan loader, shaderc and the other
desktop libraries come from the Linux system. Configure and build with the
authoritative whole-AOT inputs. The selection is the repository-normalized LF
product file, passed explicitly:

```sh
scripts/linux/build-product-vulkan.sh \
  /path/to/generated-cpp \
  /path/to/aot_program.json \
  /path/to/canonical-lf/whole_aot_functions.json \
  /path/to/exefs/code.bin \
  /path/to/exheader.bin
```

Generated C++ regeneration is deliberately disabled for this build. The
pre-generated 256 shards and their manifest are audited first, then rebuilt by
the native Linux compiler. This preserves the audited selection identity and
prevents the historical CRLF Windows cache from being mistaken for a current
product artifact.

The resulting executable is `build-linux-product/oot3d_native_game`; its
initial PICA shader cache is `build-linux-product/oot3d_pica_default.o3ps`.
The pack is intentionally non-strict on Linux: shaderc compiles a variant on
first use when it is not among the 198 pre-seeded modules. This affects only
GPU shader caching and does not add an A32 gameplay fallback.

## Run and verification

The extracted game and resource payload remain outside Git. Run a bounded
smoke test or a normal session with:

```sh
scripts/linux/run-product-vulkan.sh /path/to/whole-aot-product-package 20
scripts/linux/run-product-vulkan.sh /path/to/whole-aot-product-package
```

Linux needs two portability fixes that Windows did not exercise: Vulkan
feature definitions must follow the renderer option instead of the Windows
platform test, and early host UI polling must tolerate the brief interval in
which `PlayState` exists but its guest `Player` pointer is not mapped yet.
Neither change alters generated gameplay execution.

The first verified smoke test ran for 20 seconds and resolved 66 PICA shader
entries with zero misses. A later scene requested an additional fragment
variant, which established that the 198-entry pack is a cache rather than a
complete immutable shader closure; normal Linux runs therefore permit shaderc
to fill misses. A 30-second non-strict test completed normally with 72 cache
hits and four runtime-compiled variants. The gameplay closure audit remains
12,419 compiled functions, three host boundaries and zero residual A32 entries.

## Public runtime (plugin mode)

The product host no longer resolves the title at link time on Linux: the
direct-AOT loader `dlopen`s `triaevum_title_aot.so` beside the executable
(`/proc/self/exe`) or the `--title-plugin` path with `RTLD_LOCAL`, exactly as
Windows uses `LoadLibraryExW`. `--verify-title-plugin` and `--product-info`
therefore have the same contract on both hosts, and the stub is a build
dependency only. NRI is enabled on Linux (Xlib/Wayland WSI via SDL, NIS
upscaler, CACAO when a DXC is available); the FidelityFX and NGX SDK adapters
remain Windows-only and are not fetched elsewhere. `lsb_release` is optional
and the whole-AOT ThinLTO cache/order-file link options use lld spellings on
ELF.

```sh
scripts/linux/stage-deps.sh            # tinyxml2 CMake package if missing
scripts/linux/build-public-runtime.sh  # build-linux-public-runtime/TriAevum
```

Known blocker: `oot3d_ui/*.h` come from the untracked evidence snapshot (see
`docs/TRIAEVUM_PRECOMPILED_RELEASE.md`), so the final link of `TriAevum` is
not reproducible from a clean clone yet. Every other product object,
including the NRI renderer, compiles; the title ABI is proven by
`validate_whole_aot_toolchain.py` on Linux.

## Switch OpenGL performance proxy

The Switch product currently uses the compatibility OpenGL renderer, so its
CPU/render hot path is measured directly on Linux with:

```sh
scripts/linux/benchmark-switch-opengl.sh \
  build-switch-product/eden-game-state/data/eden/sdmc/switch/oot3dre \
  1200 300 /path/to/navi_kokiri_main_forest.oot3dsav
```

The savestate is a deterministic Kokiri Forest checkpoint at guest frame
10,600. It is restored across Windows/Linux hosts by rebinding only serialized
RomFS kernel objects to the configured immutable RomFS; unrelated external
paths remain rejected. A later paired A/B of the shared OpenGL geometry cache
produced a disabled median of 91.158468 FPS and an enabled median of 94.811548
FPS, a 4.01% gain. Geometry upload calls fell from 747,789 to 3,958 (-99.47%);
uploaded geometry bytes fell from 464,913,200 to 11,691,452 (-97.49%). Uniform
uploads were unchanged. VSync, the SDL limiter and application pacing are all
asserted off by the benchmark script.

The script exposes `OOT3D_DISABLE_OPENGL_PICA_GEOMETRY_CACHE=1` for the control
path and `OOT3D_WHOLE_AOT_BLOCK_BUDGET=<blocks>` for dispatch experiments. A
single-pass sweep identified 128 blocks as a conservative Switch candidate,
not as an accepted optimum for Linux or Windows. Exact data and evidence limits
are recorded in the cross-platform performance status.

A pre-geometry-cache sampled profile attributes 8.521 of 10.447 host seconds
to guest dispatch. SVC handling accounts for 1.175 seconds, including 1.080
seconds in IPC immediate `0x32`; GSP command-queue processing accounts for
1.056 seconds. These timings make shared whole-AOT/IPC work the next CPU
priority while the standalone NXVK probe qualifies the native Switch Vulkan
boundary.
