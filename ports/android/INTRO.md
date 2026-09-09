# Android Title Intro Bring-Up

## Scope And Status

Run the real whole-AOT title from normal boot through the existing Vulkan/NRI
runtime, not an emulator or reconstructed cutscene demo. Android uses the shared
native-presentation policy (the same extension mask as F2). Controls overlay,
Forge import UI and release qualification follow a visible intro.

Implemented: ARM64 shared game host with `SDL_main`, SDL Android Activity/Surface,
explicit packaged-title loading, app-local paths/logs, landscape orientation,
keep-screen-on, private prepared-data staging, separate native/APK builds.
Native libraries are installed by Android, never executed from writable game data.

2026-09-09 measured on SM-S931B / Adreno 830:

- Full 256-shard / 12,419-function title linked. Temporary `-O0 -g0` title build
  took about 488 seconds with one job; runtime remains optimized. This is not a
  release optimization or a qualified performance configuration.
- One worst-shard `-O1` build with debug information exceeded a 120-second pilot;
  the same shard at `-O0 -g0` took about three seconds. Both optimization and
  debug-info changed, so this does not isolate their individual costs.
- Full runtime cross-build passed; subsequent host/renderer changes rebuild and
  relink in roughly 2-6 seconds. APK rebuilds take roughly 9-14 seconds; the title
  is not recompiled. Linux is the build host; Windows only transfers/uses USB.
- Native PICA frontend tests pass on the phone; 17 Android tooling tests pass on
  the Windows host. Java/SDL APK compilation and installation pass.
- A real 122.68-second active run produced 69 guest refreshes / 35 presentations:
  guest execution 0.810 s, PICA backend 119.822 s. Whole-AOT recorded no memory
  faults, unsupported exits or retained ARM fallback. The frame-30 native
  framebuffer was black. This is **not successful intro playback**.
- Simpleperf identified driver pipeline compilation under NRI's
  `CreateGraphicsPipeline`. The NRI pipeline owner now has a device-lifetime
  `PipelineCache`, retained across pipeline resets and destroyed at shutdown.
  A subsequent active 120.132-second run produced 1,022 guest refreshes and
  512 presentations (4.26 presentations/s): guest execution 17.843 s, PICA
  backend 63.529 s and frame-start work/waits 33.826 s. Native framebuffer
  captures are nonblack and the user confirmed intact graphics, but reported
  vertical stretching and very slow playback. This is not a full-speed intro.
  Do not attribute the entire difference to the NRI cache: the baseline also
  warmed the Vulkan driver cache and the follow-up reached different draws.
- A follow-up launch occurred behind the lock screen, with no `SDL_main` entry;
  that capture is invalid as a renderer/performance test.

### Presentation And Cache Follow-Up

The Vulkan fallback swapchain (used on Android; scene rendering remains NRI)
was declaring `currentTransform` as its `preTransform`, without pre-rotating
scanout. It now requests identity when the surface supports it, matching the
existing logical-window-coordinate scanout. Android's compositor owns rotation;
game cameras, PICA coordinates and native framebuffers are unchanged. Native
pre-rotation is a later optimization, not an excuse to stretch the image.
See [Android's surface-transform contract](https://developer.android.com/games/optimize/vulkan-prerotation).
Surfaces lacking identity support still need an explicit pre-rotated scanout path.

NRI pipeline cache data now survives normal shutdown/relaunch. The shared bridge
exposes opaque byte import/export; the Vulkan host owns the separate
`nri_pipeline_cache.bin`, existing vendor/device/driver/UUID validation and
64-MiB bound. Rejected cache data retries an empty cache; cache failures never
disable rendering. Cache export occurs before NRI owner/device destruction.
No canonical shaders, draw ordering or game timing were modified.

The follow-up cross-builds and packages successfully (one-file runtime rebuild
about nine seconds). On-device capture and the user confirm correct landscape
presentation, including Link/Epona and the title scene. The bounded 120.008-second
run produced 598 presentations (4.98/s); guest execution took 19.775 s, backend
60.396 s and frame-start work 34.519 s. Normal shutdown wrote a 1,439,944-byte NRI
cache. This verifies persistence, not yet the warm-cache performance gain.

### Repeated Surface Recreation

Simpleperf during that active run identified `RecreateSwapchain` under
`StartFrame` (21.2% of sampled CPU cycles), and graphics-pipeline creation
(42.2%). These are CPU samples, not additive frame-time percentages. The caller
repeats a 640x360 logical framebuffer request while Android grants 2340x1080.
`UpdateFramebufferParameters` compared those different coordinate domains and
invalidated the swapchain every frame. Destruction also discarded native PICA
pipelines, explaining the continuing pipeline creation despite caching.

The consumer now treats identical repeated requests as inert. Changed requests
are compared against the surface-supported extent selected by `ChooseExtent`,
not blindly against requested dimensions. Window resize and Vulkan out-of-date
events retain their existing invalidation paths. No game or shader policy changes.
The fix is compiled and installed; its speedup still requires a fresh active run.
A second attempted launch was behind the lock screen and never entered SDL_main;
it must not be counted as a warm-cache performance test.

Reproduce with the same profile/capture interval before changing instrumentation:
verify `RecreateSwapchain` and pipeline creation disappear from steady-state
profiles, compare guest/presentation counts, and confirm correct native output.
Only then disable the costly effective-shader inventory for normal playback
benchmarks. The current Android activity is a bounded developer test, not the
final user-facing launcher.

### Device Rejection And Second Invalidation Path

The next real device test rejected the logical-request fix as sufficient: the
user saw no speedup and simpleperf still sampled `RecreateSwapchain` at 21.1%
and `GetOrCreateNativePicaPipeline` at 42.5% of CPU cycles. Do not describe
commit `777d39a` as a demonstrated performance win.

The separate `VK_SUBOPTIMAL_KHR` paths also unconditionally invalidated the
swapchain after acquire/present. That status allows presentation and can remain
asserted with compositor rotation. The host now queues a surface recheck on the
render thread, recreating only when selected extent/format/color space, transform
support or image-count limits actually changed. `OUT_OF_DATE`, real resize and
explicit output-mode changes retain their mandatory invalidation paths. See the
[Vulkan swapchain contract](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_swapchain.html).

Swapchain teardown now calls `DestroyPresentationPipelines`, not the full
`DestroyGraphicsPipelines`: native PICA and Shadow2D scene pipelines do not depend
on swapchain render passes and retain their own invalidation/shutdown paths.
This removes the coupling that converted surface churn into repeated scene
pipeline creation. Android cross-build and packaging pass. On-device simpleperf
no longer finds swapchain recreation or repeated pipeline construction among the
dominant costs, and the user confirms a substantial speedup, still below real
time. Native capture timestamps suggest roughly 8-17 presentations/s across
different clips; this is not a controlled sustained-frame-rate result.

The five-second profile (16,464 samples, none lost) now attributes 64.9% of CPU
cycles to `RunUntilGuestWait`. `PicaEffectiveShaderInventory::Observe` alone
accounts for 23.5% self samples. The installed title is still compiled at `-O0`;
guest memory helpers and their uninlined callers are prominent. These CPU-cycle
figures are not additive frame-time shares. The private launch profile's explicit
`--pica-effective-shader-inventory` diagnostic was removed for normal playback;
`prepare_intro_data.py` already leaves it off. Do not disable rendering features
to make this diagnostic overhead disappear.

The private interactive profile allows 600 seconds; reproducible bounded tests
still use 120 seconds and native 30 Hz without interpolation. Confirm foreground
execution throughout: a later recording caught the Activity being backgrounded
and then the phone locking, not an SDL keep-screen-on failure. That recording is
not valid performance evidence. The Android host already requests screen-on
while its window is visible; no keyguard or device security setting was changed.

### Optimized Title Qualification

Keep the title and renderer in separate build directories. The largest shard
(203, about 2.16 MB source) compiles at `-O1 -g0 -ffp-model=strict` in 21.31 seconds
on the Linux build host. The earlier slow pilot also emitted debug information;
it did not establish that optimization itself was impractical. The complete O1
title build passed (264 commands, two jobs); packaging took eight seconds. The
stripped title is 89,461,696 bytes, versus roughly 175 MB for O0. The O1 APK is
installed; the working O0 APK/build remain available for rollback and comparison.
No translated game source,
timing constants, floating-point semantics or canonical PICA shaders change.

Next measurement: install the O1 title with the same renderer, run without the
effective-shader inventory, and compare bounded runtime counters plus an active
simpleperf sample and framebuffer captures. The device was locked after install;
no O1 FPS gain is claimed until an active run is measured. Do not count lock-screen video,
interpolated frames, or an old runtime summary left behind by a force-stop.

## Build Boundaries

`native/`: module ABI/service probes and the independently compiled title.
`runtime/`: thin Android configuration around the existing root runtime target.
`app/`: SDL Activity and packaging only; no Gradle-triggered C++ compilation.
`controls/`: isolated Azahar overlay donor import, not wired into this intro app.

Prerequisites: Linux CMake/Ninja, NDK r29, SDK 35, JDK 17, Gradle 8.13. Use separate
binary directories; downloaded FetchContent **sources** may be shared.

```bash
cmake -S "$SRC/ports/android/runtime" -B "$BUILD/runtime" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD/runtime" --target oot3d_native_game \
  oot3d_native_pica_frontend_tests --parallel 2

cmake -S "$SRC/ports/android/native" -B "$BUILD/title" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -DTRIAEVUM_ANDROID_BUILD_NRI=OFF \
  -DTRIAEVUM_TRANSLATED_TITLE_DIR="$TITLE_SOURCES" \
  -DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1 \
  -DTRIAEVUM_ANDROID_TITLE_DEBUG_INFO=OFF
cmake --build "$BUILD/title" --target triaevum_android_title --parallel 1

python3 "$SRC/tools/android/stage_native_app.py" \
  --runtime-build "$BUILD/runtime" \
  --title-module "$BUILD/title/libtriaevum_title_aot.so" \
  --ndk "$NDK" --output "$BUILD/apk-libs"
gradle -p "$SRC/ports/android" :app:assembleDebug \
  -PtriaevumSdlSource="$SDL_SOURCE" \
  -PtriaevumNativeStage="$BUILD/apk-libs" --no-daemon --max-workers=2
```

`SDL_SOURCE` must be the exact SDL FetchContent source used for the native build.
The runtime wrapper accepts `TRIAEVUM_ANDROID_DEPENDENCY_SOURCE_CACHE` to reuse
already downloaded sources. Never point two builds at the same `_deps/*-build`.
Cross-build shader-pack generation accepts `OOT3D_PICA_HOST_AOT_COMPILER`; without
a host-native tool the shared on-device shaderc path remains enabled. Do not run
an Android executable on the Linux build host.

## Private Inputs And Verification

```bash
python3 "$SRC/tools/android/prepare_intro_data.py" \
  --installation "$PREPARED_FORGE_INSTALL" \
  --launch-profile "$PREPARED_FORGE_INSTALL/TriAevum.linux.launch.json" \
  --output "$PRIVATE_STAGE"
```

The tool copies only required manifest/resources/ROM-derived data and TopScreen
inputs, relocates paths, excludes desktop title binaries and saves, and uses a
fresh configuration. The generated developer profile selects native 30 Hz,
no interpolation, 640x360, bounded 120-second execution and native framebuffer
captures. Data is **not APK content and must never be published**.

Install `app/build/outputs/apk/debug/app-debug.apk` with ADB. The profile/data root
is `/sdcard/Android/data/org.triaevum.android/files`. After the app has created
this directory, transfer a private tar to `/data/local/tmp`, then stream it into
`run-as org.triaevum.android tar -xf - -C <data-root>`. Do not extract as the ADB
shell UID: Android will allow reads but deny app writes to those directories.
Do not overwrite an existing user's data. Captures/logs may likewise require
`adb exec-out run-as org.triaevum.android cat <path>` rather than `adb pull`.

Before each cold test, force-stop only this package and start
`org.triaevum.android/.TriAevumActivity` on an **unlocked** phone. Confirm a fresh
`SDL_main` log entry and foreground Surface before timing. A process ID or black
lock-screen capture does not establish game execution. Use `capture_device.py`
for bounded scrcpy recordings, plus the runtime's native framebuffer files.
Inspect `data/runtime-state.json`, not presentation video FPS, for guest/backend
timings. Stop the process after the bounded test if it did not terminate.

Private evidence for this session remains in the local Android device-probes
and captures directories, outside public source. Profiling used NDK simpleperf
`record --app org.triaevum.android`; `security.perf_harden` was restored to 1.

## Remaining Android Issues

- Landscape presentation is user-verified; qualify a full title intro at native
  speed. The second surface-lifecycle correction is visibly faster, not yet
  real-time. Older 4-5 presentations/s results describe the invalidation bug,
  not the latest renderer.
- Qualify the optimized title without diagnostic shader inventory, then profile
  the remaining guest/backend/wait costs. Separate cold-cache startup from steady
  in-scene performance, and never use captured-video FPS as game FPS.
- Full app restart after Activity destruction: desktop runtime globals are
  process-lifetime. For now use an explicit package force-stop before relaunch.
- Pinned SDL HID Android receiver needs the target-SDK exported/not-exported
  flag adaptation before USB-controller qualification; no gameplay-input claim.
- Validate Vulkan Surface loss/recreation, native audio playback, mobile output
  sizing and app close/error presentation. No gameplay/decompilation changes are
  required by the measured initial bottleneck.
