# TAS corpus activation

2026-09-12. The collected cache is now used by the private Windows Forge package
and installed Linux Flatpak, including their activated launch profiles.
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

These are private Windows and Linux activations, not a public release or Android
deployment. The portable pack is reusable with a matching renderer schema on
other hosts, but their helper/compiler binding and GPU preparation must remain
platform-specific. Never ship this GPU's driver cache to another machine.
No new Android or physical Steam Deck result is claimed.

Public package allowlists still exclude game-derived shader caches. This change
does not authorize publishing the collected payload or silently embedding it in
the frozen Forge executable.

## Cross-platform follow-up

Windows activation was subsequently rechecked through
`validate_installed_runtime`: executable/plugin/profile identities, pack hash
and installation-local cache routing all pass.

A verified private Linux handoff was transferred from
`I:/TriAevum-private-tas/citra-1796-repaired/linux-corpus-handoff.zip`
(2,369,757 bytes, SHA256
`aef194af42995d9df85303834a57e2e470774048c293178a961183a18ab452e7`).
It contains portable packs, provenance, merge tools and instructions; no native
executables or driver caches. Reproduce with the private
`I:/TriAevum-private-tas/prepare-linux-corpus-handoff.py`.

The initial SSH authorization block was resolved with explicit user approval.

## Linux activation and verification

The existing Linux pack had 889 entries, including the two scanout shaders.
Merging it with the TAS additions adds 150 entries and produces the exact
1,039-module file used on Windows, byte-identical SHA256 above. Five pack-union
tests pass on Linux.

The private Flatpak catalog now binds the seed, existing two pipeline manifests
and Linux helper for all three canonical execution recipes (baseline EUR,
catalogue EUR and adapted USA). The exercised installation is baseline EUR;
this does not claim new per-revision gameplay coverage.

- Flatpak commit:
  `0965383feba071f4afb2e2702bf837aac6bf3ef5b9bea167448347cdad4f4de4`.
- Runtime unchanged:
  `629afe741ca4ae262b5ed60c8385381ba62ee7becabc474c27b0702cfc88e5b6`.
- Private binding:
  `65b0dd193784f6e7a711969b4104898ba1a31e6a49374969f5bdbd5576ede4c1`.
- Installed seed under `data/shader-seeds/`:
  `40c246e437c57c8344fcbc36fe46d9cf685c8c9c0ce04d13ce3782ef827a47d6/portable.o3ps`.
- User installation:
  `~/.var/app/io.github.coccofresco.TriAevum/data/TriAevum/`.
- Updated private installer:
  `~/triaevum-flatpak-proof/TriAevum-Linux-tas-corpus.flatpak`, also copied to
  `~/Scrivania/TriAevum-Linux-test.flatpak`. The previous desktop installer is
  retained under the private evidence directory.

The actual frozen Forge worker imported the already extracted user inputs,
reused the prepared title and completed activation: **zero title objects
compiled**, 22/22 renderer-pass cache hits, 588/588 GPU recipes prepared.
No title or renderer rebuild was deployed. Profile and resource hashes pass
`validate_installed_runtime` in the installed Flatpak.

### Desktop context matters for GPU cache handoff

The first worker invocation was launched through SSH without the user's display
session. It produced a valid GPU cache with a different pipeline-cache UUID
from the live Wayland game on the same RTX 4060/driver. The runtime correctly
rejected it; shader-pack loading itself was already successful.

A controlled comparison using the same packaged helper, pack, recipes and
Flatpak runtime showed that importing the user's display-session environment
produces the game's UUID. Changing application-instance creation order did NOT
fix this: the experimental source change was removed, and the original packaged
helper is retained. The driver-cache format and identity checks were not changed.

Final GPU preparation used only the packaged developer GPU helper in the desktop
context, not another ROM import or title build. A second full worker invocation
was blocked by permission review; it was not bypassed. The narrower GPU-only
preparation was approved and completed **588/588 recipes, zero failures**.
`native_helper_environment` already preserves the caller's display/driver
selection environment. A regression test now checks that helper and game inherit
the same DISPLAY, WAYLAND_DISPLAY, runtime-directory, session-bus and driver
selection values. Run remote qualification with the actual desktop environment,
not an unrelated headless SSH environment.

### Final game proof

Two 180-presentation mounted Hyrule Field replays used the normal installed
`flatpak run io.github.coccofresco.TriAevum` launcher under Wayland. Both exited
0, produced native framebuffer captures and restored the permanent profile and
activation receipt byte-for-byte after removing temporary probe arguments.

- Both: 1,039 pack entries, 54 pack hits / 32 misses; 34 local SPIR-V cache hits
  and 23 pass-cache hits, zero shader compilations or failures.
- First final replay: **6,711,971 bytes of prepared pipeline cache accepted**,
  55 pipeline creations taking 38.61 ms in aggregate.
- Second: 7,033,849 cache bytes accepted; 55 creations taking 10.90 ms.
- The two 1280x720 native framebuffer files have identical SHA256
  `6b7605fc33d70463be4d0d5228993a2e0a9f3dbc51cfebaab94572d54810f645`.
  They were inspected; these tests are not performance benchmarks.

The local module cache was already warmed by earlier diagnostic runs: those had
22 shader compilations. Zero compiles in the final runs does NOT mean Forge
contains every variant. No new scenario/variant collection was added here.

Private evidence: `~/triaevum-tas-corpus-20260912/final-result.json`,
`final-gpu-preparation.json`, `game-verification.json`, and the installed
`qualification/tas-corpus-final-game*` directories. The source-API activation
probe lacked developer Python dependencies and was not used for installation;
the distributed frozen Forge worker performed the real activation instead.

Final test suites: Windows 46 tests, 44 passed / 2 optional skipped; Linux five
merge tests plus seven process-environment tests (six passed, Windows-only test
skipped). The isolated native pipeline-contract test also passed. No persistent
LD_PRELOAD probe or test process remains.
