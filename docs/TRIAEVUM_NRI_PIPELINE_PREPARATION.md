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
shader cache. No game/framebuffer equivalence run was made with this refactor
yet. The installed game, public catalogs and release packages have not been
replaced. Windows native helper/GPU execution and Android device execution remain
to qualify; no Android device was visible to local ADB. The Forge Python host
tests also pass on Windows, with real-GPU tests explicitly skipped there.

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

Private evidence:
- Linux: `/home/xander/triaevum-android-build/prepared-native-pipelines.json`,
  `prepared-capture-inventory.json`, `nri-preparation-{cold,warm}.json`.
- Host build: `/home/xander/triaevum-pipeline-prepare-build`.
- ARM64 build: `/home/xander/triaevum-pipeline-prepare-android-build`.
- Windows evidence mirror:
  `C:/Users/xander/AppData/Local/TriAevumDeveloperEvidence/shader-seed-expansion/`.

## Next Integration

1. Qualify the modified live renderer with deterministic framebuffer captures
   and confirm that it loads the prepared cache. Preparation success is not
   visual equivalence or measured first-use hitch reduction.
2. Qualify Windows and Android GPUs; connect Android's installer in-process and
   select the same physical GPU as gameplay on multi-GPU desktops.
3. Generate the **actual enabled extension profile's** instrumented recipes with
   the existing typed shader hooks. This corpus currently prepares native
   fidelity, not every Grass/toon/interpolation/MSAA configuration.
4. Then add helper/dependencies/contracts to platform packaging. Keep captured
   state, ROMs and derived packs private under the current release policy; do
   not silently distribute the developer corpus.
