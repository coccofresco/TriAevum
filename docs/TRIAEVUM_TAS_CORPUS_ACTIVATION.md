# TAS corpus activation

2026-09-12. The collected cache is now used by the private Windows Forge package
and its activated launch profile, not merely stored as a diagnostic artifact.
See [collection evidence](TRIAEVUM_TAS_SHADER_COLLECTION.md) and
[existing corpus contract](TRIAEVUM_SHADER_CORPUS_RECONNECTION.md).

## Owning boundary

`tools/triaevum_release/merge_shader_packs.py` is a developer preparation tool.
It unions portable O3PSAOT v1 packs by stage and full source identity, preserving
the existing vertex modules. It validates bounds, schema, hashes and payload
overlap, rejects conflicting binaries, and never overwrites input evidence.
It does not change PICA generation, the renderer, game logic, or draw order.

Example, with private qualified inputs:

```sh
python tools/triaevum_release/merge_shader_packs.py \
  --input previous.o3ps --input tas-additions.o3ps \
  --output merged.o3ps --receipt merged-receipt.json
```

Bind the resulting pack in a private evidence record using the existing
`bind_private_shader_corpus.py`. Preserve the observed pipeline manifests and
the platform-appropriate helper. Run Forge activation again on the existing
prepared title; binding the catalog alone does not update an activated profile.
The normal order remains portable seed, renderer shaders, local GPU pipelines,
transactional title activation. No title recompilation or new ROM extraction is
needed. Runtime misses continue to populate the same writable installation cache.

## Applied state

- Package: `J:/TriAevum-verify-20260910/package`.
- Recipe: `oot3d-eur-project-baseline-16a6b0aa`.
- Pack: **1,039 modules** = previous 887 + 150 TAS additions + 2 compiler-added
  scanout modules. The latter are not new TAS discoveries.
- Stages: 2 vertex, 519 fragment, 518 NRI fragment; descriptor schema 3.
- SHA256: `1311f12de7379be8c8b5dcc0226eff4a560831ba1f2898cb8b999c83efb8b4c8`.
- Catalog binding:
  `8342f0bea73b88a99c8a31ca0d5da33bcf10b86bbef39c433344decac2afcd8e`.
- Activated seed under `data/shader-seeds/`:
  `93e2a6d31e665e410caa861a5f80926c16e8909ca5f5d06cefc43fd49ec54d52/portable.o3ps`.
- `TriAevum.launch.json` now explicitly passes that pack with
  `--pica-aot-shader-pack` and the installation's renderer cache directory.

Private evidence and backups:
`I:/TriAevum-private-tas/citra-1796-repaired/activation/`.
The adjacent `merged-receipt.json` identifies both input packs.
Private repeatable drivers are `I:/TriAevum-private-tas/activate-corpus.py` and
`verify-corpus.py`; neither their machine paths nor game-derived payloads are
release inputs.

## Executed checks

1. Forge reused the prepared title: **zero title objects compiled**. It prepared
   22 renderer shaders and **588/588 observed GPU pipelines**, zero failures,
   on RTX 3060. The helper loaded the existing 2,069,827-byte driver cache before
   extending it. Existing writable cache entries were not cleared.
2. Actual packaged runtime, mounted Hyrule Field checkpoint, 180 presentations:
   exit 0, **1,039 entries loaded**, 66 pack hits / 20 misses; 22 requested
   runtime variants compiled and persisted, zero failures. Renderer passes:
   21 hits, zero compiles. The runtime accepted Forge's 6,713,132-byte GPU cache.
3. Second replay: exit 0; 22 local shader-cache hits, **zero shader compiles**;
   21 pass-cache hits, zero pass compiles.
4. Controlled old-pack replay, same executable/checkpoint/settings: 887 entries,
   64 pack hits / 22 misses. Framebuffer at frame 150 is **pixel-identical** to
   the new-pack run (1280x720, zero differing pixels). This is not an FPS test.
   The two extra pack hits are not proof that all 150 TAS-specific modules ran;
   the merged pack also contains the two added scanout modules.
5. Focused Python suites: 39 tests, **37 passed / 2 optional tests skipped**,
   including five new pack-union tests. Native helper and game checks above
   are separate from these unit tests. All three game runs exited normally.

## Limits

The TAS import provided new fragment modules, not new VS/FS pipeline pairings.
The preserved 588 observed recipes are therefore unchanged. Missing combinations
and effect variants still use the normal runtime cache; do not invent combinations
or claim complete-game precompilation.

This is a private Windows activation, not a public release or Linux/Android
deployment. The portable pack is reusable with a matching renderer schema on
other hosts, but their helper/compiler binding and GPU preparation must remain
platform-specific. Never ship this GPU's driver cache to another machine.
Linux tool transfer/activation remains pending as documented in the reconnection
report; no new Linux, Android or Steam Deck result is claimed.

Public package allowlists still exclude game-derived shader caches. This change
does not authorize publishing the collected payload or silently embedding it in
the frozen Forge executable.

## Cross-platform follow-up

Windows activation was subsequently rechecked through
`validate_installed_runtime`: executable/plugin/profile identities, pack hash
and installation-local cache routing all pass.

A verified private Linux handoff is staged at
`I:/TriAevum-private-tas/citra-1796-repaired/linux-corpus-handoff.zip`
(2,369,757 bytes, SHA256
`aef194af42995d9df85303834a57e2e470774048c293178a961183a18ab452e7`).
It contains portable packs, provenance, merge tools and instructions; no native
executables or driver caches. Reproduce with the private
`I:/TriAevum-private-tas/prepare-linux-corpus-handoff.py`.

The Linux host responds to SSH, but authentication with the default identity
failed. Automatic permission review denied listing the SSH identity directory;
explicit user authorization to locate/use the dedicated TriAevum key was
requested. The handoff has not been transferred or activated. Do not equate this
staged archive with Linux parity or a successful Flatpak launch.
