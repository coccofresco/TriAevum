# Bundled Forge Shader Corpus

2026-09-12. The maintainer explicitly requested the portable corpus inside every
Forge distribution, with no user selection, download prompt or cache settings.
Release preparation/publication is paused until this path is validated.

## User Contract

The user selects their ROM and starts the normal preparation. The bundled
corpus is a package resource, not a second input. Shader and GPU preparation
report through the existing overall installation progress. No new GUI control,
standalone cache wizard, SDK or title compilation is introduced.

Forge automatically imports the portable modules, prepares renderer pass
shaders, builds observed pipeline recipes for the current GPU/driver, then
activates a profile referring to that installation's writable caches. Runtime
misses extend those caches. NVIDIA/AMD driver-cache binaries are never shipped.

This instruction supersedes the previous private-only distribution intention
for this specific portable shader corpus and its observed pipeline recipes.
It does not authorize ROMs, extracted assets, saves, captures, mod archives or
driver caches in public packages. No public artifact or release was produced
by this qualification; the release audit/allowlist must explicitly catalogue
these roles when publication resumes, rather than hide them in Forge's binary.

## Shared Owner

`tools/triaevum_release/shader_corpus_layout.py::bind_shader_corpus` returns a
catalog and explicit package file records. Both Windows and Linux use it:

- Validate the portable pack with the existing binary parser and validate the
  schema/count of each observed pipeline manifest before binding.
- Bind the same corpus to every recipe in the catalog, without mutating inputs.
- Put the portable resource at `forge/shader-corpus/portable.o3ps`; bind pipeline
  JSON and the platform's helper under `forge/`. No host path reaches the catalog.
- Retain the platform renderer compiler in both the standalone and combined
  contracts: older distributed Forge reads it from the seed contract. It must
  not silently skip renderer preparation when only the new field is present.
- Keep the same existing runtime/install services; no per-platform importer or
  renderer fork is added. The package builder chooses artifacts, not the player.

Actual payload: 1,039 modules, descriptor schema 3, SHA256
`1311f12de7379be8c8b5dcc0226eff4a560831ba1f2898cb8b999c83efb8b4c8`.
There are 588 observed pipeline recipes, not 1,039 complete pipelines and not
complete-game shader coverage. See [collection](TRIAEVUM_TAS_SHADER_COLLECTION.md).

## Real Frozen Forge Qualification

Both tests used the existing distributed Forge installation worker, which is
also used by the GUI. Its only inputs were ROM/extracted ROM directory and
installation destination; no shader arguments or manual activation were used.
Windows imported the user's decrypted CCI. Linux imported the already extracted
equivalent input under Flatpak, with an isolated XDG activation directory and
the real Wayland desktop environment. These are developer-host tests, not a
clean-machine certification, physical Steam Deck test or new GUI usability test.

| Result | Windows RTX 3060 | Linux/Wayland RTX 4060 |
| --- | --- | --- |
| Portable modules activated | 1,039 | 1,039 |
| Renderer preparation, cold | 22 compiled, zero failures | 22 compiled, zero failures |
| GPU preparation, cold | 588/588, zero failures | 588/588, zero failures |
| Initial GPU cache bytes | 0 | 0 |
| Title objects compiled | 0 | 0 |
| Game accepted Forge GPU cache | 6,713,078 bytes | 6,711,962 bytes |
| First mounted replay, portable hits/misses | 54 / 32 | 54 / 32 |
| First replay new PICA / renderer-pass compilations | 32 / 2 | 32 / 2 |
| Second replay PICA / pass compilations | 0 / 0 | 0 / 0 |

Each game replay completed 180 presentations and exited normally. Native
framebuffers were inspected: world, Link/Epona, Grass and HUD are visible.
Linux cold/warm captures are byte-identical; Windows and Linux captures were
not taken at identical presentation indices, so no cross-platform pixel-equality
claim is made. These are cache-handoff tests, not FPS benchmarks. The first-run
misses above are real and must not be described as already precompiled by Forge.

## Failures Preserved

The first Windows staging attempt exposed inconsistent historical variant
bindings; the test stager rebuilt the reviewed variant records from their base.
The first real worker skipped renderer passes because its combined contract
lacked the compiler. The shared binder now preserves that compatibility, with
a regression assertion. A following run exhausted J while writing the GPU
cache; only test-generated data/cache copies were cleared and its downloaded
archive was preserved on I. The successful run started with an empty GPU cache.

The initial Linux harness timed out before worker events with a bubblewrap
mount error involving an unavailable host mount. No system mount was changed.
The existing desktop session runner successfully executed the same staged
Forge/import command. The failed log is retained, not counted as qualification.

Do not use the old Windows `J:/TriAevum-verify-20260910/package/TriAevum.exe`
as the current release runtime: it identifies `c7f9686`. This test instead uses
the updated runtime from `J:/TriAevum-verify-20260910/runtime` (`55d17e4`, SHA
`6a1f9454984b86f9b98eb8682a8713ed31d2142a0b3b0dfe8f874b0b559dae24`).

## Reproduction And Evidence

Public tests: `python -m unittest discover -s tools/triaevum_release
-p test_shader_corpus_layout.py`. They exercise all three representative recipe
bindings on both target platforms, relocation, cold/warm reuse, corruption and
driver-cache/schema rejection. They also run on the Linux host. Full Windows
release-tool discovery: 380 tests, 366 passed, 14 optional/platform skips.

Private Windows evidence: `I:/TriAevum-forge-bundled-corpus-win/`, with
`forge-worker-cold.log`, the generated catalog/profile and `game-cold` /
`game-warm`; writable test data is `J:/TriAevum-forge-bundled-corpus-data`.
Private Linux evidence: `~/triaevum-forge-bundled-corpus/`, with the generated
activation receipts, `game-verification.json`, and both game replay directories.
Drivers: `I:/TriAevum-private-tas/qualify-bundled-forge.py`,
`refresh-test-binding.py`, and `verify-bundled-linux.py`. Existing personal
installations/profiles and saves were not overwritten by these tests.
