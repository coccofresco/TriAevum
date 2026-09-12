# NRI Pipeline Preparation

Date: 2026-09-10. Baseline: `d35277c`, branch `port/linux-nri`.

## Result

The 212-scenario corpus now produces **500 distinct host pipeline descriptions**
from 709 native recipes, without booting the guest or inventing mesh/texture
payloads. Native distinctions that do not change a host pipeline collapse through
the existing structural manifest identity. All 126,612 draws still import with
zero failures. The existing 815-module portable pack supplies every shader
requested by these pipelines.

The headless tool creates **actual NRI graphics pipelines**, destroys each after
creation and persists the driver cache. It does not just compile GLSL, create
shader modules or prepare the fallback Vulkan pipeline.

Initial helper-only qualification at `eed1a02` (superseded for live-cache
acceptance by **Live Qualification** below):

| Qualification | Result |
|---|---|
| Linux RTX 4060, empty application cache | 500/500, 8.224 seconds |
| Same device/cache, validation requested | 500/500, 0.807 seconds |
| Cache payload | 5,056,819 bytes, plus 56-byte envelope |
| Actual Forge service, separate temporary cache | 500/500, 8.294 s first / 0.791 s reuse |
| Forge corruption and cooperative cancellation | Passed; cancellation prepares zero pipelines |
| Portable job/cache/raster CTest | Passed |
| Existing live planner/visual-transition test plus metadata equivalence | Passed |
| Linux device-preparation tests including real GPU | 10 passed |
| Android NDK r29 ARM64 helper and tests | Compile/link passed |
| Android live `gfx_vulkan.cpp` / `gfx_vulkan_pica.cpp` | Compile passed; pre-existing warnings only |

These are **preparation times**, not FPS, installation times or whole-game
coverage. An empty application cache does not imply an empty vendor-global
shader cache. At that initial checkpoint no game/framebuffer equivalence run or
Windows GPU preparation had been made. Both are now covered below. Public
catalogs and release packages remain unchanged; Android GPU execution remains
unqualified because no device was visible to local ADB.

## Ownership

| Layer | Files / responsibility |
|---|---|
| Generic renderer | `fast/renderer/pipeline_preparation_job.*`: bounded batches, progress, cancellation and failure accounting; no game/PICA/platform knowledge |
| Shared 3DS renderer | `fast/renderer3ds/nri_pica_pipeline_bridge.*`: one factory for live draws and `PreparePipeline`; no fake fallback handles |
| Shared 3DS renderer | `fast/renderer3ds/vulkan_pipeline_cache_store.*`: serialization, GPU/driver validation, atomic publication |
| PICA adapter | `fast/oot3d/pica_nri_pipeline_state.*`, `pica_pipeline_preparation.*`, `pica_pipeline_manifest.*`: existing raster/attachment policy and packed NRI vertex layout, shared with live rendering |
| Native capture adapter | `oot3d_native_pica_capture_inventory.cpp`, `oot3d_native_pica_vulkan_plan.*`: recorded native state into the existing planner/bridge, not a second register decoder |
| CLI host | `tools/renderer/pipeline_prepare/`: NRI device lifetime, file arguments and JSON progress/report; no SDL, window, guest or whole-AOT |
| Forge host | `tools/triaevum_release/device_pipeline_preparation.py`: verified artifacts, process lifetime, progress and optional acceleration receipt |

The `fast/oot3d` paths retain existing renderer API names but contain no title
addresses, scene IDs or asset exceptions. Android uses the **already patched NRI
checkout from its renderer build**, not another platform-specific implementation.

The metadata-only planner shares shader generation and vertex layout lowering
with live draws but skips vertex/index/texture payload copies. Tests compare
bindings, attributes and sources against a real draw plan and prove that the
live path still rejects missing resource buffers. Its result is explicitly not
drawable; only the recording backend consumes it.

## Cache Contract

- File: `nri_pipeline_cache.bin`, in the live renderer's shader-cache directory.
- Version 2: explicit little-endian header, renderer ABI, Vulkan vendor/device,
  driver version, pipeline-cache UUID, size and payload checksum; maximum 64 MiB.
- Old version-1, foreign-device, truncated, oversized and corrupt caches are
  rejected and rebuilt. This intentionally causes one cache rebuild on upgrade.
- Writes replace a same-directory temporary file atomically. Failed writes keep
  the old cache. Live runtime logging now checks write success.
- Driver cache data remains local. The portable pack is not a driver cache.
- Do not prepare concurrently with gameplay writing the same cache: atomic
  writes prevent partial files, but last-writer-wins can discard additions.

The helper now wraps a Vulkan device using the live renderer's shared
`renderer3ds/pica_vulkan_device_profile.h`: application/API contract and enabled
core/Vulkan-1.2 features. It no longer lets standalone NRI choose a different API
and feature set. Queue/surface ownership stays in the host; no window is needed
for preparation. The helper's host implementation is isolated in
`tools/renderer/pipeline_prepare/headless_device.h`.

GPU/driver identity is necessary but not sufficient. On the tested Linux NVIDIA
driver, importing the desktop display/session environment changes the reported
pipeline-cache UUID even on the same GPU. An SSH-only preparation produced an
incompatible cache; importing the same session used by gameplay produced the
matching UUID. Run the helper in Forge's inherited desktop/sandbox environment,
not in a detached environment with display variables stripped. Never rewrite
the UUID or weaken validation to force a cache to load.

Cancellation is checked between pipeline calls. A synchronous driver compilation
cannot be interrupted safely inside NRI. Forge requests cooperative cancellation
and reaps the process if it does not return, with a bounded total timeout. It
never leaves an unowned background job. Validation errors cannot count as
successful pipeline preparation.

## Forge Contract

Optional `device_pipeline_preparation` is a sibling of `shader_preparation` in a
verified title record. This example is **not an enabled catalog**:

```json
{
  "device_pipeline_preparation": {
    "format": "triaevum_device_pipeline_preparation_v1",
    "helper": {"path": "tools/triaevum_nri_pipeline_prepare", "bytes": 123, "sha256": "VERIFIED_HASH"},
    "manifest": {"path": "seeds/native-pipelines.json", "bytes": 123, "sha256": "VERIFIED_HASH"},
    "dependencies": [],
    "adapter": 0
  }
}
```

Forge prepares the portable pack, then the device pipelines, before title
activation. Missing/unverified artifacts are package errors. GPU failure or
cancellation is reported but does not prevent a playable install: normal runtime
compilation remains available. No contract means no job.

Every invocation rechecks the actual device, not just yesterday's success
receipt. Reports include GPU/cache identity, counts and times and are stored at
`DATA_ROOT/shader-seeds/device-preparation/latest.json`. Success requires complete
counts and the requested cache file. No receipt claims game coverage.

Desktop cache paths match `SDL_GetPrefPath(nullptr, "oot3d_native_vulkan")`:
Windows `%APPDATA%/oot3d_native_vulkan/shader_cache`; Linux
`${XDG_DATA_HOME:-~/.local/share}/oot3d_native_vulkan/shader_cache`. Flatpak uses
its own environment. Android's future in-process installer supplies its private
path and uses the shared job/factory, not Python or an executable spawned by Java.
The helper reports its GPU; `adapter` is an NRI enumeration index. On multi-GPU
hosts verify that it matches gameplay before enabling a package contract. A
mismatch invalidates the cache, not rendering.

For isolated tests, both Forge and the renderer accept the same optional
`TRIAEVUM_RENDERER_CACHE_DIR` absolute path. Relative paths are rejected; absent
overrides retain the normal platform directory. This is a developer isolation
mechanism, not another user-facing cache preference.

`--validation` on the helper now requires an actual Khronos validation layer;
missing layers fail explicitly rather than implying API validation from NRI's
error counter alone. Normal Forge preparation does not require a Vulkan SDK.

## Reproduce

Developer build only, using existing pinned dependencies:

```text
cmake -S tools/renderer/pipeline_prepare -B BUILD -G Ninja -DCMAKE_BUILD_TYPE=Release -DTRIAEVUM_NRI_SOURCE_DIR=PINNED_NRI -DCMAKE_PREFIX_PATH=DEPENDENCY_PREFIX
cmake --build BUILD --parallel 2
ctest --test-dir BUILD --output-on-failure
oot3d_native_pica_capture_inventory --manifest PRIVATE_CORPUS.json --output PRIVATE_INVENTORY.json --pipeline-manifest PRIVATE_PIPELINES.json
triaevum_nri_pipeline_prepare --manifest PRIVATE_PIPELINES.json --pack PRIVATE_PACK.o3ps --cache-dir PRIVATE_CACHE --report PRIVATE_REPORT.json --validation
```

Android: same project with the NDK toolchain, `ANDROID_ABI=arm64-v8a`,
`ANDROID_PLATFORM=android-29`, and its existing patched NRI source. No title or
APK rebuild is required for the helper. End users never execute CMake.

Real Forge test: set `TRIAEVUM_TEST_NRI_PREPARE_HELPER`,
`TRIAEVUM_TEST_NRI_PIPELINE_MANIFEST`, `TRIAEVUM_TEST_NRI_SHADER_PACK`, then run
`python -m unittest test_device_pipeline_preparation` in the release-tools folder.
Inputs are copied to a temporary private package; no installed cache is modified.

Live qualification uses the existing bounded probe, with savedata/configuration
copies and framebuffer readback rather than desktop screenshots:

```text
python tools/triaevum_release/probe_renderer.py INSTALLATION RUNTIME COLD_OUTPUT --profile HOST_LAUNCH_PROFILE --native-fidelity --frames 900 --seconds 90 --capture-interval 150 --shader-pack PRIVATE_PACK.o3ps
triaevum_nri_pipeline_prepare --manifest PRIVATE_PIPELINES.json --pack PRIVATE_PACK.o3ps --cache-dir PREPARED_CACHE --report PRIVATE_REPORT.json
python tools/triaevum_release/probe_renderer.py INSTALLATION RUNTIME WARM_OUTPUT --profile HOST_LAUNCH_PROFILE --native-fidelity --frames 900 --seconds 90 --capture-interval 150 --shader-pack PRIVATE_PACK.o3ps --cache-directory PREPARED_CACHE
```

Use the same desktop environment for all three commands. Each probe output must
be a new directory. `--native-fidelity` selects Authentic, fixed native ticks
and no interpolation. The seconds argument remains a safety bound, not the
measured gameplay duration. Compare the six `framebuffer_*.bmp` hashes, runtime
state fingerprints and `gpu.json/nri_pipeline_compilation`. The latter records
driver-accepted initial bytes and timed creation attempts/successes, independent
of the release logger's severity threshold. Neither cache-file existence nor
`cache_input_loaded` alone proves that gameplay used the prepared data.

## Live Qualification (2026-09-10)

- Windows MSVC helper/contract build passes. Actual Forge preparation on RTX
  3060: **500/500**, 9.815 s first preparation, 0.688 s reuse. All 13 Forge/probe
  tests pass, including GPU-accepted bytes, corruption, cancellation, Windows
  executable suffix and isolated configuration. A separate 500-pipeline Vulkan
  and NRI validation run reports zero errors (unused vertex attributes produce
  warnings from the existing live 16-attribute layout).
- Linux RTX 4060: **500/500**, 8.298 s first preparation, 0.664 s reuse through
  Forge. All 13 Forge/probe tests, the pipeline contract test, and 6 diagnostic
  tests pass. These Linux runs do **not** claim Khronos API validation: that
  layer is absent on this host. Earlier `validation_requested` reports from
  standalone NRI did not prove it was installed.
- Real Linux intro: **900 presentations, 70,391 submitted draws**, no visual
  interpolation. Six framebuffer captures at presentations 120, 270, 420, 570,
  720 and 870 are byte-identical between the pre-change runtime, the modified
  renderer with empty application cache, and the prepared-cache run. All three
  finish with process-state fingerprint `2405137468041955`.
- Gameplay accepts **5,056,331 cache payload bytes**. Its 58 NRI pipeline
  creations take **68.580 ms** with empty application cache versus **18.765 ms**
  with the prepared cache in these samples. Driver-global disk caches were not
  cleared; this is not a cold-driver benchmark, an FPS improvement claim, or
  proof that every first-use hitch is eliminated. The fallback Vulkan pipeline
  factory still has its own cost/cache.
- The tested runs report zero composition mismatches and zero rejected scene
  draws. Native fidelity remains active, with zero interpolated draws.
- The same helper builds for Android ARM64 with NDK r29 and the renderer's
  patched NRI. Subsequent Adreno 830 qualification prepares **500/500** pipelines
  in 94.635 s, then reuses the cache in 0.975 s. The updated APK also reuses the
  portable pack and pass cache. This is device qualification, not an Android
  installer UI: see [Android results](../ports/android/SHADER_PREPARATION.md).

Private live evidence is under `/home/xander/triaevum-pipeline-live-proof/`:
`baseline-resolved/`, `cold-measured/`, `prepared-live-profile/`, and
`prepare-desktop.json`. The intermediate `prepared/` run intentionally remains
as negative evidence: identical images but **zero** accepted cache bytes.
Windows helper evidence/build is in
`I:/oot3dre_work/nri-pipeline-preparation-windows/`. No captures, packs, driver
caches or executable backups belong in the public source archive.

Private evidence:
- Linux: `/home/xander/triaevum-android-build/prepared-native-pipelines.json`,
  `prepared-capture-inventory.json`, `nri-preparation-{cold,warm}.json`.
- Host build: `/home/xander/triaevum-pipeline-prepare-build`.
- ARM64 build: `/home/xander/triaevum-pipeline-prepare-android-build`.
- Windows evidence mirror:
  `C:/Users/xander/AppData/Local/TriAevumDeveloperEvidence/shader-seed-expansion/`.

## Next Integration

1. Extend the live prepared-cache qualification beyond the intro, then verify
   the packaged Windows runtime and Steam Deck/AMD environment. Linux intro
   parity and actual cache acceptance are established above, not whole-game
   hitch coverage.
2. Connect Android's installer in-process after the Adreno qualification, and
   select the same physical GPU as gameplay on multi-GPU desktops.
3. Generate the **actual enabled extension profile's** instrumented recipes with
   the existing typed shader hooks. This corpus currently prepares native
   fidelity, not every Grass/toon/interpolation/MSAA configuration.
4. Then add helper/dependencies/contracts to platform packaging. Keep captured
   state, ROMs and derived packs private under the current release policy; do
   not silently distribute the developer corpus.
