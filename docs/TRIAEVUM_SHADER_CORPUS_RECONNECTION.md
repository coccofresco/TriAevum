# Retained shader corpus: installation status

2026-09-12. Steam Deck is a conformity target, not a physically tested device.

## Finding

The historical multi-source/location corpus still exists. However, the inspected
ordinary Linux candidate catalogs only bind `renderer_shader_preparation`;
the Windows September-10 diagnostic package bound none of the three stages.
The earlier private Forge handoff tests did not enable the ordinary catalogs.
Do not interpret successful renderer-pass preparation as PICA corpus coverage.

Forge already owns the correct sequence in `precompiled_titles.py`:
portable seed -> renderer passes -> destination-GPU pipelines -> activation.
All stages share the installation's writable renderer cache. Runtime misses are
compiled and persisted there. No new title compilation or location campaign is
needed to reuse retained evidence.

## Implemented private connection

`tools/triaevum_release/bind_private_shader_corpus.py` persistently attaches an
existing trusted qualification record to ONE explicitly selected package recipe.
It copies only hashed preparation inputs, validates paths and content, preserves
other revisions and renderer preparation, and reuses the current packaged shader
compiler when present. It supports source inventories and portable SPIR-V packs;
it does not copy a foreign GPU's driver cache. Repeated binding is idempotent.

```sh
python tools/triaevum_release/bind_private_shader_corpus.py \
  --private-package PRIVATE_PACKAGE \
  --trusted-evidence QUALIFICATION_DIRECTORY \
  --recipe EXACT_RECIPE_ID
```

Evidence must contain `title-record.json` and `preparation-inputs/`. It includes
native executables: trust it as a developer build, not as an arbitrary downloaded
shader archive. Normal Forge subsequently consumes the persisted catalog through
its existing preparation and activation path; this tool does not silently alter
an already activated user's launch profile or config.

## Windows evidence

Private output: `I:/TriAevum-public/retained-corpus-20260912/`.
Original diagnostic catalog is preserved there as `original-catalog.json`.
The EUR baseline recipe in `J:/TriAevum-verify-20260910/package` now binds the
retained pack and pipeline recipes; other recipes are unchanged.

- Locally retained pack: **887 modules**, SHA256
  `2f3c215fad2a949adb786795f337859b061e608ac651e9047bd8c29201fc2e0d`.
  The later Linux qualification documents 889 including two scanout modules;
  these are not the same file. Never silently equate the two inventories.
- Current compiler prepared **22 renderer modules**, zero failures; second
  preparation reused all 22 with zero compilations.
- Existing preparation helper prepared **588/588** observed pipelines on RTX
  3060, zero failures, without booting the game. This helper was reused, not
  rebuilt; its compatibility was tested by the current runtime accepting the
  resulting 6,713,124-byte driver cache.
- Current Windows runtime completed the mounted Hyrule Field checkpoint replay
  (180 presentations, exit 0): **52 pack hits, 34 PICA misses**, and **21 pass
  cache hits plus 2 pass compilations**. Missing variants were written to cache.
  This is a cache-handoff check, NOT an FPS or visual-fidelity benchmark.
- Second identical replay: exit 0, **36 PICA cache hits and 23 pass cache hits,
  zero compilations** in both counters. The portable pack still reports its 34
  misses, correctly satisfied from learned local cache rather than recompiled.
- Focused Python suites: **43 passed, 5 optional-input tests skipped** (34 shader
  tests plus 14 pipeline-preparation tests). The real hardware preparation and
  both game replays above were executed separately, not counted as unit tests.

## Remaining boundaries

1. Merge current-renderer variants into source inventories using the existing
   capture/import tools. Rebuild against the current compiler and requalify;
   do not substitute an old successful 889-module report for current results.
2. Linux retained source corpus is at
   `/home/xander/triaevum-pipeline-live-proof/forge-pass-cache-effects/`.
   New-tool transfer to the user Linux mirror was denied by automatic approval
   review; explicit authorization was requested. Linux binding is NOT applied.
3. Public packages still exclude game-derived caches under the release policy.
   This private reconnection is not a public-release solution: a permitted
   ROM-derived reconstruction/import mechanism must be completed separately.
   Do not rename private inventories as neutral resources to evade the audit.
4. Keep missed shaders' normal runtime cache enabled. Validate early/late states
   incrementally. The historical 1,692-entry scenario catalog is NOT proof that
   all locations, cutscenes, bosses or hardware combinations were covered.

No Android/Steam Deck hardware result or new release is claimed here.
