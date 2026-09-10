# Android Shader Preparation

## Boundary

Android uses the same PICA pack loader, persistent pass compiler, NRI pipeline
factory and cache identity rules as Windows/Linux. Java owns lifecycle and the
future installer UI, not a second shader implementation. No Python, SDK or
desktop compiler is required in the eventual user APK.

`tools/android/prepare_intro_data.py` now carries a prepared installation's
optional `--pica-aot-shader-pack` unchanged, validates path containment and adds
an app-local `--renderer-cache-directory`. It deliberately does not copy host
driver caches. Its default 120-second test has **no framebuffer captures**;
`--capture` enables a separate visual test. Never overwrite existing saves or
configuration with a fresh developer stage.

## Device Qualification: 2026-09-10

Device: Samsung SM-S931B, Android 16, Adreno 830. Linux builds the NDK r29 ARM64
runtime/APK; Windows is only the authorized USB bridge. The O1 title module is
unchanged. Runtime sources start from `a714dcb`; this change adds a shutdown
cache receipt, not rendering or gameplay changes.

The standalone ARM64 preparation helper ran the shared contract tests, then
prepared the private 500-native-pipeline manifest against an 889-module portable
pack, **without booting the game**:

| Preparation | Success | Total time | Pipeline creation | Accepted input |
|---|---:|---:|---:|---:|
| Empty application cache | 500/500 | 94.635 s | 94.368 s | 0 bytes |
| Same-device reuse | 500/500 | 0.975 s | 0.710 s | 13,517,298 bytes |

Zero failed pipelines. This is not Khronos validation, a cleared driver-global
cache benchmark, game coverage or an FPS claim. The file includes a 56-byte
identity header in addition to its driver payload. Desktop GPU cache bytes were
never installed on the phone.

## Real Intro

The updated APK used the prepared portable pack and this Adreno's cache, with
native lighting/fog/TopScreen intact, advanced effects disabled by the Android
policy, 1560x720 actual output, native 30 Hz and **no interpolation**. Vsync and
30 Hz pacing are enabled: these runs qualify native-speed playback, not maximum
uncapped throughput. The user confirmed intact landscape rendering.

| Same APK, 120-second run | PICA pack | Pass shader cache | Native game updates | Presentations |
|---|---|---|---:|---:|
| First app use | 143 hits, 0 misses | 20 compiles/writes, 1.411 s | 3,033 | 3,067 |
| Second app use | 143 hits, 0 misses | 20 hits, 0 compiles | 3,574 | 3,600 |

Both report zero PICA runtime shader compilations and zero shader-cache failures.
The second run takes 120.0004 s: 29.78 logical updates/s and 30.00 presentations/s.
There is no interpolation inflating those counts. Its worst presentation
interval is 128.3 ms, RMS interval error 2.75 ms, and 143 deadlines are missed
without deadline resynchronization. Native-speed average is established for
this intro sample; perfectly uniform pacing and full gameplay are not.

The first run contains a 5.31-second maximum interval and 12 deadline
resynchronizations. Its 25.55 presentations/s whole-run mean includes first-use
costs; do not describe it as steady-state gameplay or attribute all differences
to pass compilation (only 1.411 s). No repeated screen capture or video recording
ran during either test. A single Android screenshot after the first bounded run
had ended is not renderer failure evidence. SDL lifecycle logs confirm shutdown
after the run, not a pause during it.

`TRIAEVUM_NRI_PIPELINE_CACHE` now emits a JSON shutdown receipt even in release
hosts that suppress INFO logs: `initial_cache_bytes`, `creation_attempts`,
`created`, `creation_nanoseconds`. It reads the same bridge statistics as the
existing GPU diagnostics; it does not add per-frame I/O. Use accepted bytes,
not cache-file existence, to establish actual game-side driver consumption.

A subsequent isolated run with that receipt, a fresh application cache directory
and **only the helper-produced NRI cache** confirms the game accepts all
13,517,298 prepared payload bytes. Its 80 NRI pipeline object creations still
take 5.238 s; accepted cache bytes do not imply full recipe coverage or free
object creation. The rebuilt module also recompiles 20 pass shaders (2.081 s),
as expected from the changed compiler fingerprint. This first-use sample has
long stalls and is not a steady-state performance result. Its receipts are
`android-prepared-receipt.json` and `.stderr.log` in the private evidence folder.

The same instrumented APK's second run accepts 14,114,865 bytes including the
newly persisted entries. Its same 80 object creations take **150.753 ms**, with
143 pack hits, 20 pass-cache hits and zero reported shader compilations/failures.
In 120.0025 s it presents 3,600 frames and executes 3,485 logical updates
(29.04/s); worst interval 174.2 ms, RMS error 3.83 ms, 386 missed deadlines and
zero deadline resynchronizations. These two warm samples establish roughly
29.0-29.8 native updates/s, not guaranteed 30 Hz simulation under all conditions.
CPU/frame-start costs vary between runs; no thermal or hardware-limit conclusion
is justified by this cache test. Receipts: `android-prepared-receipt-warm.*`.

## Reproduction And Evidence

Developer-only GPU preparation, with already staged ARM64 tools/private inputs:

```text
triaevum_nri_pipeline_prepare --manifest prepared-native-pipelines.json --pack portable.o3ps --cache-dir cache --report cold.json
triaevum_nri_pipeline_prepare --manifest prepared-native-pipelines.json --pack portable.o3ps --cache-dir cache --report warm.json
```

Use bounded ADB shell execution. Import private app data with `run-as
org.triaevum.android`, not the shell UID, so cache/config writes remain possible.
Force-stop only this app before each new process. Read completed runtime JSON,
`logs/native-stderr.log` and lifecycle logs before stopping an unfinished probe.
Use `capture_device.py` only for separate bounded visual checks; do not revoke
ADB authorization or restart its server to repair an app failure.

Private receipts/APKs/profiles are under
`I:/oot3dre_work/android-shader-preparation/`. Linux runtime build:
`/home/xander/triaevum-android-build/runtime`; title:
`/home/xander/triaevum-android-build/title-o1`. The build mirror is
`/home/xander/triaevum-android-source`; its old Git HEAD is not the deployed
source version. Before synchronization, changed files were compared against the
public source and overwritten versions backed up to
`/home/xander/triaevum-android-build/pre-a714-source/`. Do not use the mirror's
HEAD alone as a reproducibility identifier.

## Remaining Work

1. Wire Android installation to the **shared in-process** preparation job with
   progress/cancellation. The ADB helper proof is not a shipped Android Forge UI.
2. Prepare common pass shaders in that same APK module before gameplay. Android
   statically links shaderc into `libTriAevum.so`; its conservative compiler
   fingerprint differs from a host or standalone helper. Copying their pass
   caches would not qualify first-launch reuse. First use currently compiles
   20 pass shaders, then reuses them; a rebuilt module invalidates that cache.
3. Measure residual pipeline creation and frame waits separately. Zero shaderc
   calls does not mean zero driver pipeline creation or guaranteed hitch-free
   presentation. Extend known recipes from actual misses, not duplicate caches.
4. Qualify controls, lifecycle/audio and gameplay separately. No change in this
   cache tranche claims those features complete, or enables a public seed catalog.

Known private packs, ROMs, captures and driver caches remain outside public
packages. The public release input/provenance policy is unchanged.

Verification: Android runtime incremental build and APK assembly pass without
rebuilding title code. The focused Python staging/capture/handoff/preparation
suite runs 35 tests: 34 pass, one optional private-GPU integration test is skipped.
Actual Adreno helper and game runs above are separate from that synthetic suite.
