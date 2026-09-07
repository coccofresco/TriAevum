# TriAevum development and release workflows

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

> Archived direct-IR/ABI-v1 strategy. Statements below about forbidding local
> generated C++ or completed clean-machine qualification do not apply to v2.
> The authoritative workflow is [TRIAEVUM_PRODUCT_WORKFLOW.md](TRIAEVUM_PRODUCT_WORKFLOW.md).

## Non-negotiable split

Development and end-user installation are separate products. Development may
use the audited generated-C++ whole-AOT oracle for fast iteration. A public
release must generate the private title module directly from structural IR and
must not require generated C++, a source checkout, CMake, Visual Studio, vcpkg
or a general project build.

Public artifacts contain only the generic TriAevum runtime, Forge, its
title-neutral LLVM tools and generic module adapter, recipes and license
material. `code.bin`, title assets, structural IR, native objects, `game.tam`,
caches, captures and saves remain private local artifacts.

## Development workflow

Use the narrowest build surface affected by a change:

| Changed surface | Rebuild |
| --- | --- |
| NRI renderer, host UI, audio or input | Incremental public runtime target only |
| Stable module ABI or OoT3D host adapter | Private game-module target, reusing the AOT archive |
| Whole-AOT runtime headers | Only invalidated cached AOT objects, then game module |
| Translator semantics or selected title code | Structural IR, invalidated AOT objects, then game module |
| Supported title revision | Full private Forge preparation and equivalence audit |

The normal developer loop is therefore:

1. Keep one verified private Forge content directory and one content-addressed
   translator/object cache outside the repository.
2. Run `prepare-aot-ir` only after title input or translator changes. Identical
   source and translator identities reuse the audited IR immediately.
3. Use `build-aot-fallback` for the current generated-C++ development oracle.
   It compiles optimized ThinLTO objects without debug payload, caches every
   object by source/dependency/toolchain content and only recreates the archive
   when necessary.
4. Link or rebuild only the private module. Runtime-only changes reuse it.
5. Run focused tests first, then the module vertical smoke test, then the
   playable whole-AOT oracle only when the affected boundary requires it.

Example development fallback:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 prepare-aot-ir `
  --prepared-directory <private-content-directory>

.\scripts\triaevum\Invoke-TriAevumForge.ps1 build-aot-fallback `
  --prepared-directory <private-content-directory> `
  --generated-directory <audited-generated-cpp-directory> `
  --llvm-root <pinned-llvm-root> `
  --nlohmann-include <verified-include-root> `
  --jobs 4
```

`build-aot-fallback` is deliberately named and reported as a development
fallback. Forge audits its complete source/program/function/file identity
before compiling it. Its object/archive contract is producer-neutral so the
direct structural-IR emitter can replace the C++ producer without changing
module linking, packaging or cache ownership.

## End-user release workflow

The target installation is a single guided Forge action:

1. Install or unpack the signed public TriAevum package.
2. Select a lawfully obtained, decrypted `.3ds` or `.cci` ROM. This is the only
   file requested by the guided workflow. Forge never downloads a title, keys
   or firmware and never modifies the selected source.
3. Extract code, ExHeader and raw RomFS with the integrated bounds-checked
   NCSD/NCCH reader, verify their revision, derive the process/content index
   and publish private state atomically under the portable `data/` directory.
4. Produce and audit path-independent structural IR from the verified local
   `code.bin` and ExHeader.
5. Emit native objects directly from structural IR with the pinned,
   redistributable Forge codegen component and package the private title
   companion with the generic adapter in `game.tam`.
6. Activate Play only after every input, IR, native image and TAM identity
   verifies.
7. Reuse the content-addressed module on subsequent starts. Runtime updates do
   not rebuild it unless the stable module ABI or translator identity changed.

The final Forge package contains a small native translation/link component or
the required redistributable LLVM subset. It does not invoke a separately
installed compiler. Generated C++ is forbidden from this path.

## Time and reproducibility budgets

On a supported modern desktop, acceptance targets are:

- first verification, IR generation, direct code generation, link and
  self-test: at most four minutes, with visible stage progress;
- unchanged reinstall or launch: at most ten seconds before runtime boot;
- renderer/runtime update with compatible module ABI: no title-code rebuild;
- interrupted work: no partially valid artifact and no damaged prior cache.

Every stage records source, recipe, translator, toolchain, ABI, target and
optimization identities. Cache keys exclude host paths. Equal verified inputs
must produce equal semantic identities and deterministic native artifacts;
where an external binary format prevents byte identity, the manifest records
and tests the exact normalized difference.

## Failure behavior

Forge fails before execution on an unsupported or changed input, corrupted
cache, incompatible ABI or failed self-test. It retains the last verified
module, writes new outputs atomically and gives a local diagnostic without
asking users to upload private logs to an unrelated service.

## Current implementation boundary

| Capability | State |
| --- | --- |
| Verified private input and content index | Complete |
| Stable TAM/module and typed host-service boundary | Complete |
| Structural whole-AOT IR from explicit private inputs | Complete |
| Audited incremental ThinLTO C++ fallback for development | Complete |
| Direct structural-IR-to-object backend | Complete; full title cold/cached builds verified |
| Self-contained Forge application | Complete; one-file binary and full IR extraction verified |
| Install-relative generic runtime | Complete; active-title NRI/audio/input/filesystem vertical verified |
| End-to-end clean-machine install and boot | Complete; isolated dump-to-open-title run verified |

Measured release evidence and exact complete-title counters are in
`TRIAEVUM_DIRECT_AOT_RELEASE_VERTICAL.md`.
