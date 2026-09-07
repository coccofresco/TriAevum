# TriAevum release architecture

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Objective

Ship a reusable PC runtime without distributing original or translated title
code or title content. A user with a supported lawful dump performs a short,
local preparation step. Runtime upgrades must not rebuild the game module, and
configuration changes must not rebuild either component.

This is an engineering boundary, not a legal conclusion. Ownership of a copy
does not by itself settle every copyright or anti-circumvention question.

## Artifact boundary

| Artifact | Contents | Built by | Public |
| --- | --- | --- | --- |
| `TriAevum.exe` | Generic host, services, renderer, audio, input and module loader | Project CI | Yes, after all release gates pass |
| `TriAevumForge` | Input verifier, extractor/indexer, translator and cache builder | Project CI | Yes, after all release gates pass |
| `forge/oot3d_game_module.dll` | Generic title adapter and stable host-service client | Project CI | Yes |
| `forge/clang-cl.exe`, `forge/llvm-lib.exe`, `forge/lld-link.exe` | Title-neutral LLVM compiler/archive/link support | Project CI | Yes, with LLVM notices |
| `forge/triaevum_title_whole_aot_support.lib` | Guest-memory and VFP support without title code | Project CI | Yes |
| `triaevum_title_aot.dll` | Empty ABI stub in the pristine package; private whole-AOT plugin after Forge | Project CI / Forge | Stub only / No |
| `game.tam` | Generic adapter, locally translated title companion and binding metadata | Forge | No |
| `content.tap` | Content-addressed index of the user's extracted title data | Forge | No |
| `process-manifest.json` | Verified CTR process and physical-memory layout derived from the user's ExHeader | Forge | No |
| `pipelines.cache` | Locally derived PICA/NRI pipeline cache | Forge/runtime | No |
| user configuration and saves | User preferences and game state | Runtime | No |

`tam` means TriAevum Module; `tap` means TriAevum Package index. The formats
must be title-neutral and versioned.

## Runtime ABI

The runtime loads a title module through a small stable C ABI. The first ABI
must cover only contracts already represented by the current host boundaries:

- module identity, format version, target triple and required runtime ABI;
- title initialization, frame/update entry and clean shutdown;
- guest memory declaration and checked address translation;
- host services for files, time, threads, HID, audio and PICA command
  submission;
- serialization hooks for portable save states;
- bounded, lease-based access to module-owned guest memory for renderer and
  service adapters;
- explicit diagnostics and fatal-error reporting.

Loading and execution are separate phases: verified module metadata is
available while the host-service registry is still mutable, allowing generic
runtime adapters to be composed from declarations rather than title branches.
No module initialization occurs until that registry is complete and sealed.

No title address, scene rule, TopScreen condition or game-specific renderer
branch belongs in the generic ABI. ABI compatibility is checked before any
module code executes.

The concrete v1 container and C declarations are documented in
`TRIAEVUM_MODULE_ABI.md`. The title-neutral lifecycle implementation is
documented in `TRIAEVUM_RUNTIME_SESSION.md`.

## Forge pipeline

1. Accept a user-selected decrypted `.3ds` or `.cci` cartridge image in the
   guided path, or explicit extracted inputs in the developer CLI. Never
   download a title, key or firmware.
2. Bounds-check the NCSD/NCCH partition and ExeFS tables, reject encrypted
   partitions, then extract and hash code, ExHeader and raw RomFS without
   modifying or retaining the source ROM.
3. Match only those extracted identities against a public supported-revision
   recipe containing hashes and structural metadata.
4. Derive the CTR process image, entrypoint, heap and physical-memory mappings
   from the verified ExHeader and content. Forge rejects a manifest whose
   source hashes or recipe contract do not match the selected revision.
5. Build and audit the path-independent structural whole-AOT IR from those
   explicit verified inputs. Publish it atomically in a content-addressed
   translator cache; a local development IR may only seed this stage after the
   same complete product audit.
6. Generate the audited whole-AOT C++ in 256 stable affinity shards and compile
   it through the content-addressed ThinLTO object cache. Link only those
   private objects with the narrow title-neutral memory/VFP support library.
7. Package the public generic adapter and private title companion into the
   local `game.tam`. Do not relink `TriAevum.exe`.
8. Prebuild known PICA pipelines and allow a bounded local runtime cache for
   states not covered by the initial corpus.
9. Write outputs atomically under a content-addressed local directory. Record
   every source/tool hash and never modify the source dump.
10. Atomically install the private ABI-v2 plugin and a launch profile that
    selects NRI/Vulkan and TopScreen, then run a local self-test before exposing
    the Play action.

The cache key includes source identities, recipe, translator, runtime ABI,
target triple and optimization profile. An unchanged key reuses the module in
seconds.

## Public package construction

Public packaging is allowlist-only. Every included file has a declared role,
size and SHA-256 in a generated release manifest. The audit rejects unknown
files as well as known forbidden names, extensions, metadata and hashes.

The public package may contain only:

- generic runtime and Forge executables/libraries;
- title-neutral runtime resources and default configuration;
- supported-revision recipes that contain hashes and structural facts but no
  title bytes;
- source offer/source archive and all required licenses/notices;
- user documentation.

The default build excludes proprietary NGX/DLSS binaries and SDK material.
Optional integration may discover a separately installed user component only
after its API and redistribution terms receive a dedicated review.

## TopScreen handling

Typed, independently written behavior may remain in the title adapter where it
was reconstructed from observed interfaces. The original IPS, injected ARM,
CTXB replacements and derived texture atlas are not public runtime assets.
The base OOT3D installation now imports these textures locally after verifying
the official archive's identity. Forge acquires that archive from the author
or reuses a local copy; neither the archive nor the derived atlas becomes a
public package payload. Executable patches are not applied. The lower-level
runtime still supports native game textures without the replacement pack.
See `TRIAEVUM_TOPSCREEN_INSTALLATION.md` for the implemented installation path.

## Whole-AOT oracle and release path

The static developer executable remains non-redistributable and is retained as
an equivalence oracle. The public runtime is built from the same mature host in
plugin mode: its pristine DLL is an empty title-neutral stub, and Forge replaces
that file locally with the user-derived ABI-v2 whole-AOT plugin. Exact process,
memory and framebuffer fingerprints have been matched between both paths.

## Acceptance gates

A public release is permitted by project policy only when all gates pass:

1. Runtime builds in a clean environment with no private inputs mounted.
2. Forge and runtime are independently versioned and the runtime boots no title
   without a local module.
3. The direct module backend reproduces the current whole-AOT golden suite and
   strict counters with no A32 fallback.
4. The public package audit reports an exact allowlist match and zero forbidden
   content.
5. A clean-machine test starts from only the public package and a user-selected
   supported dump, then reaches the validated boot/title path.
6. Source, licenses, notices, build instructions and corresponding-source offer
   satisfy every donor obligation.
7. GPL-3.0-or-later scope, donor notices and the maintainer's legal-review
   approval are recorded in the release evidence.

## Delivery phases

1. **Policy and audit:** branding, provenance inventory, package allowlist,
   forbidden-content scanner and CI gate.
2. **Forge foundation:** immutable input verification, content-addressed local
   workspace, recipes, diagnostics and atomic manifests.
3. **Runtime split:** title-neutral executable, stable module ABI and private
   local content provider.
4. **Fast local code generation:** direct IR-to-object `game.tam` backend and
   incremental pipeline-cache generation.
5. **Equivalence and release:** golden suite, clean-machine Forge test, source
   compliance bundle and legal review.

Each phase must leave the previous whole-AOT oracle runnable. No phase may
weaken the public package audit to make an incomplete artifact pass.

Current gate status is machine-readable in
`tools/triaevum_release/release_readiness.json`. The public packager refuses to
run while any required gate is incomplete. The concrete whole-AOT ownership
split is documented in `TRIAEVUM_WHOLE_AOT_MODULE_MIGRATION.md`; separate
developer and end-user procedures are defined in
`TRIAEVUM_DEVELOPMENT_AND_RELEASE_WORKFLOWS.md`.
