# Android Native Foundation

Status: developer cross-build, not a playable APK or a supported release target.
Build on Linux with the Android NDK; the existing Windows PC may provide the USB
ADB connection without hosting a second compiler/SDK installation.

## Boundaries

- Retain the existing Vulkan/NRI renderer, native PICA rendering, gameplay and
  shared service interfaces. No Android-specific renderer or new decompilation.
- `NativePresentationPolicy` applies the exact desktop F2 mask on Android:
  Grass, Toon/outline, CACAO and reflections Off. This is not the Authentic
  preset. Configured values, interpolation, FOV, native lighting, UI composition
  and unrelated options remain unchanged. Desktop F2 remains reversible.
- Platform lifecycle, surfaces, input devices, storage and audio-device binding
  belong to the Android host, outside title logic and canonical PICA semantics.
- The publisher cross-compiles the existing manifest-verified translated C++
  title for `arm64-v8a`. No x86 module is reused. Players do not install a compiler.
- ROM import must use Android's document picker and a native import service.
  Document URIs are not filesystem paths. The desktop Tk Forge UI is not ported.
- The release catalog deliberately does not advertise Android yet. An ARM64
  library that links is not proof of renderer presentation or playable boot.

## Linux Build

Qualified compiler for this foundation: Android NDK r29 (`29.0.14206865`),
Clang 21; CMake >= 3.30 and Ninja. API 29 is the initial build floor, not a
claim that all API-29 GPUs satisfy the renderer's capability requirements.
NDK r29 produces 16-KiB-aligned ELF binaries; check every packaged native library,
including `libc++_shared.so`, rather than assuming all dependencies are aligned.

```sh
export ANDROID_NDK_HOME="$HOME/triaevum-android-tools/android-ndk-r29"
export TRIAEVUM_BUILD_DIR="$HOME/triaevum-android-build/arm64"
export CMAKE_BUILD_PARALLEL_LEVEL=3
bash scripts/build-android-native.sh
python3 -m unittest tools.triaevum_release.tests.test_translated_title_sources
# Separate, expensive publisher operation; never part of routine host iteration.
export TRIAEVUM_TRANSLATED_TITLE_DIR="$HOME/triaevum-linux-deps/title-source"
TRIAEVUM_BUILD_TITLE=1 bash scripts/build-android-native.sh
```

The native target builds the shared module services, existing memory/VFP support,
the real plugin loader, native-presentation policy, pinned NRI Vulkan core and
their probes. With title sources supplied and `TRIAEVUM_BUILD_TITLE=1` it also builds
`libtriaevum_title_aot.so`. Full source compilation is a publisher cost, separate
from fast incremental renderer/host builds.

NRI is pinned to the same revision as desktop. A small idempotent CMake patch
admits Android, whose Vulkan loader branch already exists upstream. This build
does not enable NRI's optional desktop SDK integrations. It does **not** yet
compile the full TriAevum PICA renderer or Android window host.

## Device Verification

Use `adb devices -l` before deployment. A USB/MTP device without an ADB entry
needs debugging enabled and the host authorized on the unlocked device.
Do not expose the ADB server on a public/network interface. Use local USB,
or a loopback-only SSH tunnel when Windows is the USB bridge.

`native_presentation_policy_tests`, the module service tests and loader probes
are ARM64 Android executables: do not run them on the Linux x86 host or count
successful cross-compilation as passing on-device tests. Deploy them with the
NDK's ARM64 `libc++_shared.so` to a private `/data/local/tmp` test directory.

`triaevum_android_nri_probe` checks adapter/device/graphics queue creation only.
It is not a framebuffer or performance test. The actual renderer wraps a device
with its own capability negotiation; the standalone NRI creation defaults are
not an authoritative mobile device-compatibility gate.

## Next Deliverable

1. Integrate the existing PICA Vulkan renderer in the Android build, including
   cross-compiled shader compiler dependencies, without forking render logic.
2. Add the APK host: surface recreation, pause/resume, app-local module/data
   paths, audio-device lifecycle and shared input routing.
3. Add ROM selection/import and package the precompiled ARM64 title and runtime.
4. Test real on-device startup, native framebuffer output, audio, input and
   resume. Qualify capability requirements and performance on actual hardware.

Keep the currently installed Linux Flatpak and desktop distribution untouched.
Toolchains, ROMs, game-derived data and build directories are never public source
or APK payloads. Preserve existing donor licenses and source-distribution rules.

## Evidence (2026-09-09)

- Linux-hosted r29 cross-build passed for the pinned NRI core/device probe,
  actual title memory/VFP support and loader, module services, input/audio/storage
  tests, policy test and empty/mock shared modules. ELF inspection identifies
  Android API 29, AArch64 and `/system/bin/linker64`; the inspected shared module
  has 16-KiB `PT_LOAD` alignment.
- Host policy test passed; shared inventory parser passed seven positive/negative
  tests. The real desktop F1/Controls/TopScreen widget smoke passed 1,761
  assertions after rebuilding the changed runtime components in Steam Runtime 4.
- Full title compilation was attempted from the existing verified 256-shard
  inventory, not regenerated or decompiled. Several shards compiled, but individual
  ARM64 optimized compilations took minutes and up to approximately 3.2 GiB RSS.
  The full-title build was stopped deliberately with completed objects retained;
  there is no linked/qualified Android game module yet. It is explicitly excluded
  from the default rapid build. Investigate compiler timings before release builds.
- USB transport is detected by Windows, but ADB lists no authorized device yet.
  Consequently none of the ARM64 probes has been executed on the phone. No APK,
  Android framebuffer capture, gameplay or mobile performance claim is established.

References: [NDK downloads](https://developer.android.com/ndk/downloads/index.html),
[16-KiB page support](https://developer.android.com/guide/practices/page-sizes),
[platform tools](https://developer.android.com/tools/releases/platform-tools),
[renderer architecture](../../docs/OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md).
