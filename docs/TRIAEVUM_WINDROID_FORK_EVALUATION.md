# WINDROID fork evaluation record

RECOMP-ZELDA-3DS-ANDROID (<https://github.com/WINDROID-EMU/RECOMP-ZELDA-3DS-ANDROID>,
WINDROID-EMU) is a fork of TriAevum at `e419028` (v0.6.0-alpha.2c, 2026-09-12)
with 73 commits of its own (2026-09-18 to 2026-10-17, 229 files, +33k lines)
that port the runtime to Android as a native Activity (ANativeWindow, AAudio,
libadrenotools, a Windroid touch overlay) and add gameplay conveniences. The
fork keeps a modified copy of the alpha.2c tree, so none of its commits apply
to the current renderer (the fork predates the NRI split of `gfx_vulkan.cpp`);
every adopted item below was rewritten against the current tree. Evaluated on
2026-10-23; the full survey (all 73 commits tabulated) is kept outside the
repository with the alignment coordination notes.

Provenance: the fork keeps TriAevum's GPL-3.0 license, third-party notices and
donor manifest unchanged. Adapted hunks are modifications of a GPL-3.0 program
and are distributed under this repository's license; the adapted code is marked
in place (`Adapted from the WINDROID fork ...`). Credit is recorded in
`THIRD_PARTY_NOTICES.md` and `docs/TRIAEVUM_CONTRIBUTIONS.md`.

## Adopted (batch 1, 2026-10-23)

| Change | Fork commit | TriAevum form | Verification |
| --- | --- | --- | --- |
| Periodic pipeline-cache flush while pipelines are still being created | `857081f` | `Renderer3ds::PipelineCacheFlusher` (own below-normal thread, atomic store, at most one write per 20 s and only when new pipelines exist); used by the game device (`TRIAEVUM_NRI_PIPELINE_CACHE_FLUSH=0` restores the shutdown-only store) and by Forge's pipeline helper, whose work now survives a kill or an exhausted time budget | contract test (`triaevum_pipeline_preparation_tests`), Kokiri gate with an empty renderer cache, flush lines in the console |
| Vertex arena overflow continues instead of ending the session | `c24df3a`, `95b5f00` | `EnsureFrameVertexCapacity`: the per-frame vertex/upload arena is replaced by a larger buffer and the old one retired until the frame's fence, as the uniform arena already did; the five former `throw` sites (legacy draw, Shadow2D, native transient geometry, texture upload) use it. `TRIAEVUM_VULKAN_VERTEX_ARENA_KIB` (16..1048576, diagnostic) sets the initial size to exercise the path (the NRI path keeps geometry and textures in its own buffers, so the arena carries about 70 KB per frame in the Kokiri gate against a 64 MiB default); the shutdown line `TRIAEVUM_VULKAN_VERTEX_ARENA summary` reports the initial size, the peak per-frame use and the growth count | Kokiri gate at the default (64 MiB) and at a 32 KiB arena (four growths to 128 KiB in the first twelve frames, each logged as `TRIAEVUM_VULKAN_VERTEX_ARENA_GROWN`): identical frames |

| Texture-pack filename tolerance (batch 2, 2026-10-23) | `18b97ed`, `6f7be68` | `ParseAzaharTextureFilenameHash` accepts the three-field `tex1_WxH_HASH` form of older Citra packs and ignores the letter case of the prefix and hash; the pack.json mapping, the old/new hash choice and the negative cache (`mFailedLoadHashes`) already existed in the current tree | `oot3d_graphics_foundation_tests` (`IndexesThreeFieldAndMixedCaseFilenames`) |
The fork's mutex around `vkCreateGraphicsPipelines` was not taken: the NRI
bridge creates pipelines concurrently by design and the pipeline cache is
internally synchronized.

## Candidates for a later batch

- Texture-pack upload robustness (`95b5f00`, `c24df3a`): reviewed 2026-10-23,
  nothing to take. The current tree already has a per-frame custom upload
  budget (`kCustomTextureUploadBudgetBytes`), reports PBR material upload
  failures back to the pack runtime and keeps failed loads out of the retry
  path; the fork's record removal targets its own pre-NRI texture cache.
- Texture-pack filename tolerance: adopted (batch 2, above). The fork's
  preload-all (`e3cbb62`) is not wanted.
- Non-fatal UI subsystem render error (`12c98dd`): skip the subsystem for the
  frame and count it, if the current tree still throws there.
- `--threads` and pack reuse in the PICA AOT compiler (`6c25788`), developer
  tooling only.

## Not adopted

- The parallel "Fase 0-10" PICA renderer tree (29 files, about 8k lines):
  compiled into the runtime library but referenced by no product code; its
  "golden frame" claims are synthetic unit tests.
- Commit `202477d`: stubs the TopScreen verification entry points to
  `return true`, masks a GSP interrupt failure and fabricates the language list.
- Always-on sprint, cutscene skip, auto-save and the cheats tab: gameplay
  semantics outside TriAevum's fidelity contract (sprint has no enable flag).
- The 2.1.1-era HUD layout hunks (superseded by SingleScreen 3.1), 90/120 Hz
  and uncapped pacing, and the settings-persistence bypasses. The final
  commit's advertised "lighting and dynamic shadows fixes" are Android
  settings bypasses and logging; the diff contains no shader or lighting change.
- The Android platform layer itself (native Activity, AAudio, libadrenotools,
  touch overlay): reference material, like the Ocarina ReTuned packaging. The
  fork removes SDL on Android where TriAevum's SDL path already has the
  fullscreen fixes adapted from ReTuned, so the two are not combined.

## Policy findings (for the record, nothing inherited)

The fork commits an 18.7 MB archive of translated title C++ to git, downloads a
ROM from a CI secret to translate and compile the title on GitHub Actions and
publishes a release on every push, bundles TopScreen texture overrides and
intro media without provenance, and lets the Android configuration manager
bypass engine validation. TriAevum's public release invariant forbids all of
this; see `docs/TRIAEVUM_PRECOMPILED_RELEASE.md`.
