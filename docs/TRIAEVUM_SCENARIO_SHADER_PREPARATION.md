# Scenario-Driven Shader Preparation

## Purpose

Reuse native entrance/setup injection to collect shader preparation inputs,
without manual playthroughs or scripted navigation. Recover existing captures
first, add small targeted captures for missing resources, and pass everything
through TriAevum's production PICA frontend and NRI shader variant generator.
This is developer tooling. Forge must not boot an emulator on users' machines.

## Implemented Flow

1. The existing scenario adapter requests a native transition from a seed save.
   The original loader constructs the scene. Scene IDs, entrances and cutscene
   selectors come from the existing decomp-derived catalog, not guessed actors.
2. Opt-in Azahar `OOT3D_PICA_DUMP_SHADER_SEED=1` records full PICA registers,
   live uniforms, program/swizzle bytes, and resident lighting/procedural/fog
   LUT words before draws. Each frame is self-contained. Unchanged resources
   are emitted once per frame; changed LUTs are emitted before their consumers.
   No forced software vertex capture, texture readback or full register-write
   history is required for this mode.
3. `recover_pica_capture_corpus.py` reads successful matrix rows or individual
   summaries, preserves resource/draw order and original files, and produces
   a relocatable, SHA-256-indexed JSONL corpus. Completed empty frames are
   recorded as skipped; incomplete frames are errors. Missing retained evidence
   is explicitly excluded. Output vertices and unrelated diagnostics are omitted.
4. `oot3d_native_pica_capture_inventory` emits the existing effective shader
   inventory schema, plus observed native pipeline recipes. It calls the same
   vertex/fragment generators and NRI variant builder as the renderer.
5. The existing AOT shader compiler merges inventories by exact stage/source
   identities and compiles the portable `.o3ps` pack. No Cartesian VS/FS product
   is generated and no MMJ GLSL is substituted for canonical PICA lowering.

Program bytes can complete historical hash-only draws when both program and
swizzle hashes and their lengths match. Conflicting payloads are fatal. Live
uniforms remain per draw; dynamic LUTs never cross frame boundaries. Historical
draws without LUT payload use the existing explicit `OfflineSource` contract:
lighting values are runtime data, but referenced procedural LUTs cannot be
invented. Resource-complete draws use the normal `RuntimeDraw` validation.
Unsupported native geometry shader lowering is reported, not silently covered.

`complete_import` / `complete_draws` mean both shader stages were generated
successfully, **not that the captured draw was replayed or visually validated**.
Pipeline recipes retain their native register snapshot and distinguish full
resource captures from historical inputs. A later complete observation upgrades
the recipe's snapshot with provenance; it does not rewrite the earlier draw.

## Verified On 2026-09-09

| Measurement | Result |
| --- | --- |
| Historical captures recovered | 23 nonempty frames, 482 draws, 8 scenarios |
| New targeted captures | Kokiri, Death Mountain Crater, Hyrule Field cutscene setup, Water/Spirit/Shadow Temples |
| Combined corpus | 131 nonempty frames, 4,235 draws, 13 scenario IDs |
| New draws with complete resource snapshots | 3,753 |
| Vertex payload identities | 1, also resolves all 482 historical draws |
| Successfully generated draw shader pairs | 4,235 / 4,235; zero failures |
| Distinct generated sources | 1 vertex + 76 fragment + 76 NRI fragment = 153 |
| Observed native pipeline recipes | 172; 120 have a complete-resource observation, 52 remain historical |
| Union with Citra seed and existing boot inventory | 745 modules, all compile to SPIR-V |
| Portable pack size | 5,969,016 bytes |
| Import / union compilation on Linux development PC | approximately 4.1 s / 6.4 s |

The previous Citra + boot union contained 743 modules. This campaign adds only
**2 new modules** to that union, not 153. Its other concrete gain is real stage
pairings and raster state. This is not a percentage of whole-game coverage.
The three temple captures took 11.4, 11.2 and 11.0 seconds respectively.
Resuming that completed matrix took about 2 seconds and launched no emulator.
These are preparation measurements, not game FPS or device-pipeline timings.

The Hyrule Field sample is catalog setup `spot00_info_setup_3_entry_00cd` with
native cutscene selector `0xFFF3`, captured immediately after ready. It is not
claimed to be a visually verified title-intro timeline or all cutscene states.

Verification: 8 existing/extended coverage-tool tests, 4 corpus recovery tests,
the native capture adapter test executable, and 5 opt-in real-importer tests
pass. The latter exercise complete source generation, recovery from a later
program payload, missing programs, cross-frame LUT isolation and conflicting
program rejection. Original private capture files remain unchanged.
All six new scenario launches completed and were stopped by the launcher.
No title/game rebuild, Android deployment or new visual/performance parity
claim is part of this work.

## Reproduce And Extend

### Expanded Representatives, 2026-09-09

The complete representative campaign now has **102/102 launchable scenarios
captured**, zero game/capture failures, and 9 catalog-only entries explicitly
classified as unavailable. It contains 1,099 nonempty frames and 27,240 draws.
All 27,240 draw shader pairs import successfully: 449 source modules and 654
native pipeline recipes. Compared with the preceding 745-module portable pack,
this adds **70 modules**, producing an **815-module union**, all compiled to
SPIR-V. Existing modules are retained even when absent from this new sample.

The six original targeted captures are no longer the extent of the collection.
The separate 110-setup temporal campaign uses three windows at 0/4/12 seconds.
Its results must be reported separately from completed representatives until
the whole temporal matrix and its native import have finished.

Representative evidence and analysis are in
`I:/oot3dre_work/shader-seed-expansion/representatives/`,
`representatives-inventory.json`, `representatives-analysis.json` and
`representatives-compile.json` in the parent directory. Larger temporal capture
outputs use `C:/Users/xander/AppData/Local/TriAevumDeveloperEvidence/shader-seed-expansion/`
to avoid exhausting I:. These remain private development artifacts.

The representative campaign exposed a Windows checkpoint sharing-lock error.
No capture was lost: pending completed checkpoints recovered both interrupted
updates, without repeating their emulator runs. Atomic replacement and its
bounded failure path are now tested separately from the game.

### Commands

Build isolated tools, not the game:

```text
cmake --build BUILD --target oot3d_native_pica_capture_inventory oot3d_native_pica_azahar_capture_tests oot3d_native_pica_aot_compiler --parallel 2
```

PowerShell, with the instrumented emulator and an existing seed in slot 5:

```powershell
./scripts/oot3d/Test-Oot3dAzaharCoverageMatrix.ps1 `
  -Mode scene_representative `
  -AzaharExe INSTRUMENTED_AZAHAR -RomPath PERSONAL_ROM `
  -Slot 5 -Backend vulkan -ShaderSeed -CompactEvidence -SkipFramebuffer `
  -CaptureFramesOverride 12 -TimeoutSeconds 75 -RunDirectory PRIVATE_RUN
```

This visits all 111 representatives and classifies the 9 catalog-only entries
without launching them. For temporal coverage of the 110 native setup variants:

```powershell
./scripts/oot3d/Test-Oot3dAzaharCoverageMatrix.ps1 `
  -Mode setup_variant -AzaharExe INSTRUMENTED_AZAHAR -RomPath PERSONAL_ROM `
  -Slot 5 -Backend vulkan -ShaderSeed -CompactEvidence -SkipFramebuffer `
  -SettleMilliseconds 0 -CaptureFramesOverride 12 `
  -CaptureWindowOffsetsSeconds 0,4,12 -RunDirectory PRIVATE_VARIANT_RUN
```

Offsets are host-wall-time sampling requests relative to the first capture,
not guest animation-frame assertions. Every window records its actual trigger
time and frame range. The game runs normally between samples. An optional
framebuffer image still refers to the first window only. The coverage summary
reports which native pipeline identities first appear in subsequent windows.
The first 12 seconds do not establish full-timeline coverage of long cutscenes,
boss deaths or interactive states that the requested setup never reaches.

Omit `SeedSavestatePath` to use the already installed slot without copying it.
`-ShaderSeed` always retains native payloads even with `-CompactEvidence`;
derived diagnostic conversions remain disabled. The summarizer rejects seed
mode when the executable fails to emit resource events. Reuse `RunDirectory`
to resume; use `-RetryFailures` for failed scenarios. Mixing an old non-seed
matrix with a seed campaign, or changing its sampling schedule, is rejected.
Checkpoint publication uses atomic replacement with bounded sharing-lock
retries; a complete newer `.tmp` checkpoint is recovered on resume. This avoids
losing a completed capture when Windows briefly locks the report for reading.
Unattended seed captures stop their own emulator after all capture-end records
are present; they do not wait on hidden exit-confirmation dialogs.
`ScenarioListPath` can select the
existing greedy cover; `setup_variant` selects cutscene setups. Adjust settle
and frame windows deliberately for transitions, rather than blindly waiting
until a transient effect has ended.

The matrix produces `shader-corpus/corpus.json` automatically. To combine it
with earlier campaigns, run recovery with repeated `--matrix` / `--summary`.

```text
python tools/oot3d/native_a32_runtime/recover_pica_capture_corpus.py --matrix OLD_MATRIX --matrix NEW_MATRIX --output-root PRIVATE_CORPUS
oot3d_native_pica_capture_inventory --manifest PRIVATE_CORPUS/corpus.json --output PRIVATE_INVENTORY.json
python tools/oot3d/native_a32_runtime/summarize_pica_shader_campaign.py --inventory PRIVATE_INVENTORY.json --baseline PREVIOUS_INVENTORY.json --output PRIVATE_ANALYSIS.json
oot3d_native_pica_aot_compiler --inventory PRIVATE_INVENTORY.json --inventory OTHER_INVENTORY.json --pack PRIVATE_PACK.o3ps --manifest PRIVATE_COMPILE.json --merged-inventory PRIVATE_UNION.json
```

Importer exit 0 means complete source import; 3 produces an explicitly partial
report; 1 means malformed/conflicting input or an I/O error. Never activate a
partial pack as if it were complete. For opt-in integration tests, set
`TRIAEVUM_CAPTURE_IMPORTER` to the tool and `TRIAEVUM_CAPTURE_CORPUS` to the
private manifest, then run `test_capture_inventory_integration.py -v`.
The native importer also reports pipeline IDs per frame. Campaign analysis
uses those observed relationships to count genuinely new modules and select a
deterministic compact set covering the observed native recipes. It does not
infer relationships from a shader Cartesian product or claim whole-game
coverage. Capture scheduling and checkpoint I/O have isolated PowerShell tests:
`Test-Oot3dAzaharCaptureSchedule.ps1` and `Test-Oot3dCoverageCheckpoint.ps1`.

## Remaining Work

- Expand scene representatives and native setup variants only where useful.
  The existing catalog has 111 scene representatives, 102 launchable and 9
  catalog-only. Its 1,692 entries are not 1,692 verified playable states. Boss
  entrances alone do not prove coverage of spawn, death or other transient
  effects; use evidenced native selectors, not arbitrary memory guesses.
- Turn the observed recipes into a renderer-owned prepare-only job using the
  actual NRI pipeline creation path, including host target formats, sample
  counts, layouts and extension profile. Native snapshots alone do not specify
  a complete device PSO. Persist with device/driver/backend/ABI-aware keys.
- Connect that job to Forge with progress/cancel and bounded batches; qualify
  first launch, reuse and invalidation on Windows, Linux and Android. Shader
  compilation success is not proof that driver compilation hitches are gone.
- Keep game-derived captures, inventories and packs private. No public title
  catalog or release is enabled by this change. A distributable seed or a
  user-ROM-derived recipe needs the separate release decision described in
  [Forge shader preparation](TRIAEVUM_FORGE_SHADER_PREPARATION.md).

## Implementation And Evidence

- Capture/inventory adapter: `tools/oot3d/native_pica_frontend/`.
- Recovery/catalog tools: `tools/oot3d/native_a32_runtime/`.
- Scenario runners: `scripts/oot3d/*AzaharCoverage*.ps1`.
- Private results: `I:/oot3dre_work/shader-seed-recovery/` (`combined/`,
  `combined-inventory.json`, `scenario-union-compile.json`, `scenario-union.o3ps`).
- Historical matrices: `I:/oot3dre_work/azahar-coverage-matrices/`,
  `smoke_full_20260825_2300` and `scene_representative_sample_5_20260825`.
- Linux importer/tests: `/home/xander/triaevum-android-build/capture-importer`
  and `capture-tests`; compiler:
  `/home/xander/triaevum-linux-build/oot3d_native_pica_aot_compiler`.
- Instrumented Azahar: `I:/oot3dre_work/azahar-oot3d-coverage`, branch
  `oot3d-coverage-injection`, commit `23d77c2d4`, based on `1833641d5`.
  The reproducible source-only delta is
  `tools/oot3d/native_pica_frontend/instrumentation/azahar-shader-seed.patch`.
  It extends the existing diagnostic fork, not a clean upstream checkout.
  Preserve Azahar/Citra donor attribution and source licensing.

The Windows diagnostic build used MSBuild's `video_core.vcxproj`, then
`citra_meta.vcxproj`, both Release/x64, `BuildProjectReferences=false`,
`CL_MPCount=2`, `/m:2`; final link also `PostBuildEventUseInBuild=false`.
Build root: `I:/oot3dre_work/azahar-oot3d-coverage-build`. `citra_qt` is a
library here, not the final executable. No Qt, whole-AOT or title rebuild is
needed to iterate on this capture mode.
