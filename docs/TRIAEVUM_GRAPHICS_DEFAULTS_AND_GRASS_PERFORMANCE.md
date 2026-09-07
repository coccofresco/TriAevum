# Graphics defaults and grass performance

Updated 2026-09-07. Baseline: project `476f384dc`, renderer `93ba845f`.

## Product defaults

`tools/oot3d/native_game_runtime/triaevum_product_graphics.inc` holds the
maintainer-approved snapshot from the September 7 playable session:

- Visual interpolation x2 (30 Hz native logic, 60 Hz presentation target).
- Global scene FOV 1.10x.
- Complete current Toon and outline settings, including custom lighting bands.
- Complete current grass generation, appearance, masks, wind, interaction,
  culling and independent segment-distance LOD settings.
- All five native source texture rules: `be15aff93dfdcd88`, `2321986eb9820c29`,
  `4b8941fd174516b0`, `0a29e93a3b0742b3`, `bd769b9ce136d73a`, including dimensions
  and their separate mask parameters.
- Grass density 1048.8/m2, 500,000 visible-blade budget, 50,000-unit draw distance,
  5,000 density reference, five near segments, one far segment, segment transition
  from 0 to 1396 units. Minimum spacing 2, individual randomness 0.
- Toon is `PicaMaterial`, band softness 0.106 and first band level 0.151.
  The exact float32 snapshot includes all outline, wind and collider settings.

`triaevum_product_info.cpp` merges this title-owned snapshot into the existing
serialized renderer defaults and deserializes it through the normal schema.
It derives `GrassSavedPreset` from the same grass block, without duplicating the
snapshot. Forge consumes `--product-info` through `product_contract.py` when
creating a new config. No new runtime sidecar, build dependency or installer
download is required. Existing valid installations retain their settings.

Do not move title-specific texture identities into generic renderer presets.
Authentic remains native/effects-off. Unrequested defaults (AA, AO, reflections,
window extent, texture-pack paths) are not copied from the development machine.

For this maintainer's installation only, the requested groups and saved grass
preset were also updated in the installed config and last interactive config.
Their unrelated settings were retained, including TAA/SSSR in the latter.
Private backups and the immutable measurement config are under
`I:/oot3dre_work/grass-performance-20260906/`.

## Reproduced slowdown

Checkpoint: `I:/oot3dre_work/native_game/checkpoints/hudtest.oot3dsav`.
Executable: `I:/oot3dre_work/triaevum-direct-module-build/TriAevum.exe`.
Profile: `I:/TriAevum-0.6.0-candidate-r1/TriAevum.launch.json`.
Same private config: 2560x1440, scale 1, Toon/outline, dense grass, TAA, CACAO,
FidelityFX SSSR, audio and TopScreen. Benchmarks use wall-clock gameplay,
uncapped presentation, VSync off, 60 warmup frames excluded, 25-second bounded
runs. No framebuffer capture or builds during measurement. GPU diagnostics
are bounded in memory and flushed at shutdown, not synchronously per frame.

| Private run directory | Presentations/s after warmup | Conditions |
| --- | ---: | --- |
| baseline-on | 4.80 | Original config, grass on |
| baseline-on-monitored | 5.05 | Repeat, grass telemetry enabled |
| baseline-off | 4.37 | Only grass disabled |
| no-reflections | 22.63 | Only reflections disabled; diagnostic isolation |
| 720p-correct | 64.70 | Same effects/grass, output reduced for diagnosis only |
| residency-on | 4.52 | Experimental memory priorities; ineffective, reverted |
| freed-vram-on | 46.76 | User closed other applications; original full config |
| freed-vram-off | 42.35 | Same freed-memory conditions, only grass disabled |

Reject the directory `720p`: its override used the wrong JSON field and did
not change resolution. Only `720p-correct` uses `Graphics.Output` correctly.

Before the user closed other applications, `nvidia-smi` reported approximately
10.2 GB of the 12 GB GPU already occupied with the game stopped, rising to
about 11.3 GB during the run. Windows process counters attributed over 6 GB to
Photoshop and two File Pilot processes. Afterwards the stopped-game reading
was about 6.6 GB. No unrelated processes were terminated by the agent.

The same 1440p workload then recovered by about 9.75x **without a renderer
optimization or quality reduction**. The evidence strongly identifies system
VRAM pressure/residency as the cause of the catastrophic regression, not the
grass generation density. GPU samples changed approximately as follows:

- TAA: about 30 ms to 0.6 ms.
- Reflections: over 120 ms to 6.1 ms.
- Total GPU frame: about 200 ms to 15.9 ms.
- Grass itself: about 2 ms before and after.

Grass telemetry confirms 504,408 cached anchors, 163,635 evaluated candidates,
156,204 visible blades, 12 draws, GPU compaction and no repeated static upload
after warmup. Selection is around 1 ms, dynamic upload about 0.89 MB/frame.
The framebuffer `visual-current/framebuffer.bmp` independently verifies the
populated scene and HUD. Its capture-run FPS is not a performance measurement.

The off/on totals must not be subtracted to estimate grass cost: scene effects
depend on the pixels covered by grass, and host/system load also varies.
Use the scoped GPU timestamps and grass telemetry. Diagnostic history is a
bounded rolling window, not necessarily the first frames of a long run.

## What is and is not resolved

The 4-5 FPS regression is no longer present after freeing VRAM. This is not a
claim of 60 FPS at 1440p with every optional effect: the full original config
still measured about 47 FPS. Do not silently lower grass density, resolution,
TAA or reflection quality to report a speedup. Do not ship speculative fixes.

A capability-checked `VK_EXT_memory_priority` experiment prioritized frame
images over cached textures; it did not improve this case and was removed.
Reference: [Khronos memory priority guide](https://docs.vulkan.org/guide/latest/extensions/VK_EXT_memory_priority.html).
No renderer changes from that experiment remain in the source or executable.

For further optimization, first repeat the matched test with sufficient GPU
memory and inspect the measured remaining frame cost (particularly SSSR and
CPU/GPU scheduling). Resource-lifetime/working-set reduction is preferable to
reducing grass quality; any change must preserve effect graph ownership and be
demonstrated in the same scene/configuration, including camera movement.

## Verification

`test_product_graphics_defaults.py` checks snapshot scope, both masks and every
grass category. Set `TRIAEVUM_PRODUCT_TEST_EXE` to the built executable to also
verify the complete exported configuration, through its actual C++ parser.
The release unit suite passes; the public runtime build is incremental and does
not rebuild title/AOT code. No public candidate binary was overwritten outside
the catalogued release process; the maintained development launcher uses the
updated executable above.
