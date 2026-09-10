# Forge Shader Handoff

2026-09-10. Builds on `09f6202` (PR compatibility and pipeline recipes) and
`298a9a1` (persistent SPIR-V cache). This is the Forge-first implementation,
not a new game-time prewarm queue.

## Product Behavior

1. Forge verifies the catalogued shader inputs and compiler dependencies.
2. Known native and extension source inventories compile into one deduplicated
   portable `.o3ps` pack. An unchanged input/tool contract reuses that pack.
3. The headless NRI helper merges known pipeline recipes and prepares them on
   the destination GPU. Native and instrumented recipes remain distinct; no
   Cartesian product of shaders, settings and attachments is constructed.
4. Forge activates the title only after these preparation stages. The launch
   profile selects both the prepared pack and the same writable renderer cache.
5. The game tries the pack first. Uncovered shaders compile through the existing
   compiler and immediately enter the existing persistent SPIR-V cache. Native
   pipeline creation also extends the driver cache. Subsequent launches reuse
   these entries. Pack misses are not fatal in the normal installation profile.

GPU preparation is an acceleration step: unavailable hardware or interrupted
preparation may leave a playable installation using normal cache misses.
Unverified/missing catalog artifacts remain package errors, not permission to
download an SDK or compile title code.

## Owning Code

- `tools/triaevum_release/shader_preparation.py`: adds `source_inventories` to
  the existing seed contract, alongside `portable_pack` and `citra_transferable`.
  Uses the existing PICA AOT compiler, including its inventory union/deduplication.
- `tools/triaevum_release/device_pipeline_preparation.py`: accepts `manifests`
  (1-64 verified artifact records), or the backward-compatible single `manifest`.
  One device job and one cache serve the whole native/extension recipe union.
- `tools/renderer/pipeline_prepare/main.cpp`: repeated `--manifest`, schema and
  structural collision checks, bounded deduplication, shared NRI lowering and
  pipeline factory. Reports input recipes separately from unique pipelines.
- `tools/triaevum_release/precompiled_titles.py`: connects preparation to normal
  installation before title activation; no gameplay or title build is used.
- `forge.py`, `installed_runtime.py`, `migrate_installation.py`: pass and preserve
  `--renderer-cache-directory`; validate its routing, not its mutable contents.
- `tools/oot3d/native_game_runtime/oot3d_native_game{,_bootstrap}.{cpp,h}`:
  accepts the host cache path and forwards it to the existing renderer owner.
- `runtime/three_ds_recomp/src/fast/renderer/spirv_cache.cpp` and
  `renderer3ds/vulkan_pipeline_cache_store.cpp`: unchanged persistence and
  compatibility authorities. No second SPIR-V cache or pipeline scheduler.

Installation data owns `cache/renderer`, not the executable/package directory.
This supports portable Windows and writable Flatpak app data with the same
contract. A bounded best-effort copy preserves the previous SDL cache on first
migration; the old cache is untouched. Existing installation caches are never
overwritten by migration. The renderer rejects incompatible compiler/driver
entries as before. Deleting the cache does not invalidate saves or installation.

`source_inventories` requires `compiler`, nonempty `inventories`, optional
`dependencies`, and `descriptor_schema_version`, using the existing verified
artifact records (`path`, `bytes`, `sha256`). Native and extension inputs may
coexist. The device contract accepts either `manifest` or `manifests`, never both.
The Forge GUI receives stage/progress messages through its existing worker.

## Qualification

The private corpus combines the 815-module native seed with the 77-module
effects inventory: **887 unique modules**, not 892. The pipeline union is
**588 recipes** (500 native + 88 instrumented, including outline occlusion).

Linux RTX 4060, real Forge services, real NRI preparation, then two bounded
900-presentation game launches per case:

| Case | First launch | Second launch |
|---|---|---|
| Native, full Forge pack | 96 PICA pack hits, 0 misses; 2 common renderer shaders compiled | 96 hits, 0 misses; 0 compiled, 2 disk-cache hits |
| Current effects, full Forge pack | 129 pack hits, 0 misses; 2 common shaders compiled | 129 hits, 0 misses; 0 compiled, 2 cache hits |
| Deliberately incomplete native-only pack, effects enabled | 51 hits, 84 misses; 72 total modules compiled and written | 49 hits, 80 misses; 0 compiled, 82 cache hits |

Misses count resolution requests, not unique shaders. The incomplete-pack case
proves that uncovered variants remain functional and persist, rather than
mistaking a complete seed for successful fallback coverage. It spent 11.013 s
inside shader compilation on first use, then zero on the second launch.
The two common shaders in the complete cases took 0.478/0.635 s and then zero.
These are compiler timings, **not FPS gains**. Interpolated runs have variable
request counts and are not deterministic screenshot comparisons.

The six native framebuffer captures at presentations 120, 270, 420, 570, 720,
870 are byte-identical across launches and to the preceding accepted native
baseline. The first native run accepted 6,711,979 bytes of prepared NRI cache;
its 58 live pipeline creations took 19.37 ms in total. Pipeline object creation
still occurs in the game; a driver cache accelerates it, not serializes live
GPU handles between processes.

Windows RTX 3060: the same portable pack prepares 588/588 pipelines. Supplying
the native manifest twice yields 1,088 input recipes but still only 588 creates.
Validation reports zero errors; unused vertex input/output interface warnings
remain. The validated first helper run takes 4.44 s, the non-validated warm run
1.15 s. Different validation settings/global driver caches make these
qualification timings, not a controlled performance benchmark.

The prepare-only helper builds for Windows MSVC, Linux Clang and Android ARM64
NDK r29. Contract tests pass on Windows/Linux. No new Android device or complete
Windows game-install qualification is claimed. The focused Python suite has
49 passing tests and 2 optional private-input tests skipped in its default run.
These counts exclude the real GPU/game checks above.

## Reproduce

`tools/triaevum_release/qualify_shader_handoff.py` stages private verified inputs,
runs the actual Forge preparation services, verifies pack reuse, then launches
the bounded renderer probe twice. It checks pack hits, accepted device cache,
zero recompilation on second launch, and deterministic capture equality when
`--native-fidelity` is selected. It never changes the user's live configuration
or saves. Specify the compiler's bundled dependencies with `--dependency`.

```text
python tools/triaevum_release/qualify_shader_handoff.py
  --output NEW_PRIVATE_DIRECTORY --compiler PICA_COMPILER --helper NRI_PREPARER
  --inventory NATIVE_INVENTORY --inventory EFFECTS_INVENTORY
  --manifest NATIVE_MANIFEST --manifest EFFECTS_MANIFEST
  --installation PRIVATE_INSTALL --runtime CURRENT_RUNTIME
  --profile EXISTING_LAUNCH_PROFILE --native-fidelity --frames 900 --seconds 90
```

Private Linux evidence: `/home/xander/triaevum-pipeline-live-proof/` under
`forge-handoff-native`, `forge-handoff-effects`, `forge-handoff-missing-variants`.
Windows evidence: `I:/oot3dre_work/forge-handoff-windows/`.

## Remaining Scope

- The technical handoff works; **no public release catalog has been enabled**.
  Known game-derived inputs/packs remain private under the release policy. A
  release must supply an explicitly permitted seed or a local ROM-derived
  reconstruction recipe, and bundle the matching preparation tools. This is
  packaging/input provenance work, not a missing cache mechanism.
- Two common renderer shaders still compile on first use; move their shared
  source preparation into Forge without copying shader definitions.
- The covered extension recipes are observed typed variants, not every future
  setting combination. New variants correctly use persistent fallback.
- Source identity lookup still follows canonical GLSL source construction;
  that does not compile GLSL on pack hits. A validated descriptor-to-module
  fast lookup is a separate optimization, not required for this handoff.
- Android shares the NRI helper core, but its installer needs an in-process host
  and device qualification. Do not ship Python/desktop executables in the APK.

PR #11's input and authorship remain recorded in
`TRIAEVUM_PR_COMPAT_PERFORMANCE_20260910.md` and `TRIAEVUM_CONTRIBUTIONS.md`.
