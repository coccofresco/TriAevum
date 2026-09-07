# TriAevum Forge

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

## Current capability

Forge currently provides the release-safe preparation and packaging boundary:

- integrated bounds-checked extraction from decrypted `.3ds` and `.cci` NCSD
  cartridge images, including ExeFS `.code` decompression;
- exact size and SHA-256 validation for the extracted code, ExHeader and RomFS;
- automatic verified local import of TopScreen 2.1.1 HUD/menu textures, using
  the official download or a local archive (see `TRIAEVUM_TOPSCREEN_INSTALLATION.md`);
- a local file-identity hash cache so unchanged large inputs are not rehashed;
- a deterministic content key and private `content.tap` index;
- an explicit, content-addressed structural whole-AOT IR stage driven only by
  the verified `code.bin` and ExHeader recorded in `content.tap`;
- atomic IR-cache publication, concurrent-writer rejection and sub-second
  reuse without invalidating the legacy 256-shard oracle cache;
- an audited incremental ThinLTO object/archive cache for 256 generated-C++
  affinity shards, now used by the playable release path;
- a whole-AOT plugin ABI that executes that generated code directly in the
  mature runtime without runtime A32 decoding or packed-operation replay;
- a versioned, hash-checked `game.tam` container writer for a locally generated
  generic native image plus private title AOT companion, including explicit
  service and physical-memory declarations;
- required PICA, audio, filesystem and input service declarations for the
  current private module;
- a content-addressed private module cache with short Win32-safe paths;
- an atomic `build-title` command that compiles, packages and activates the
  private title, installs `triaevum_title_aot.dll`, writes
  `TriAevum.launch.json`, and reuses content-addressed results.

It does not copy title bytes, download content, handle keys, bypass encryption
or claim that an index alone is playable.

## Usage

Double-click `TriAevumForge.exe` to open the guided desktop setup. The window
asks only for a decrypted `.3ds` or `.cci` ROM from the user's supported copy.
Both extensions use the same NCSD/NCCH import path. Forge extracts the ExHeader,
decompresses the native ExeFS `.code` when required, retains the raw native
RomFS, detects the supported revision from their identities, and performs the
private local compilation without blocking the UI. It enables `Launch game`
only when installation succeeds.

Forge does not contain decryption support and rejects encrypted partitions. It
does not need an external extraction program: all public tools are integrated
in `TriAevumForge.exe` or shipped under `forge/`. Generated title inputs,
translation caches, modules and configuration stay under `data/` beside the
application. The runtime discovers that portable data directory automatically;
the original ROM is never modified or copied into it.

The advanced command-line interface remains available whenever an explicit
subcommand is supplied. Its separate extracted-input arguments are retained for
development, automation and reproducible release testing; normal users do not
need them.

The OOT3D title module enables the reconstructed TopScreen profile by default.
The normal precompiled installation now also prepares its modified native
textures automatically. The typed behavior does not execute the archive's IPS
or ARM payload. In game, `F1` toggles the renderer configuration window on the
default NRI/Vulkan backend.

## Cartridge import validation

The importer is covered with generated NCSD fixtures under both `.3ds` and
`.cci` names, malformed-container bounds checks, case-insensitive suffix tests
and the common case where a decrypted image retains its legacy crypto flag.
An extraction run against the canonical local OOT3D NCCH partition produced
the exact recipe identities: 4,567,040-byte `code.bin`, 2,048-byte ExHeader and
479,260,672-byte raw RomFS, with all three SHA-256 values matching
`supported_revisions.json`. This also exercises the compressed ExeFS `.code`
decoder against real title data.

Report capabilities and recipe IDs:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 doctor
```

Verify already extracted inputs:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 verify `
  --recipe oot3d-eur-project-baseline-16a6b0aa `
  --code <path-to-code.bin> `
  --exheader <path-to-exheader.bin> `
  --romfs <path-to-romfs.bin>
```

Create the private local index:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 prepare `
  --recipe oot3d-eur-project-baseline-16a6b0aa `
  --code <path-to-code.bin> `
  --exheader <path-to-exheader.bin> `
  --romfs <path-to-romfs.bin>
```

Prepare and audit the structural whole-AOT IR:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 prepare-aot-ir `
  --prepared-directory <directory-returned-by-prepare>
```

An existing local `aot_program.json` may accelerate migration from the
development oracle with `--seed-program <path>`. Forge never trusts that file:
it audits the complete source identity, 12,422-function closure, 161,341-block
dispatcher and zero-residual-A32 contract before publishing it. The resulting
artifact is keyed by source bytes and translator semantics, not host paths.
The validated local run generated the complete IR from the prepared private
inputs in 61.6 seconds. It produced the same SHA-256 as the oracle IR; importing
that 57.7 MB oracle as an audited seed took 4.3 seconds, and subsequent reuse
took 0.19 seconds.

Compile, package and activate the private title in one step:

```powershell
.\TriAevumForge.exe build-title `
  --prepared-directory <directory-returned-by-prepare>
```

The release places pinned LLVM tools and title-neutral support under `forge/`,
so the normal installed command needs no compiler path and never rebuilds
`TriAevum.exe`. Forge compiles 12,419 functions and 161,332 dispatch entries
into 257 cached COFF objects and links the private ABI-v2 companion. The
generated source was verified byte-for-byte against the mature whole-AOT
product, and the resulting dynamic path produced identical memory, process and
both framebuffer fingerprints. An unchanged invocation reuses the generated
files, objects, archive and final DLL. The older 129-object packed-operation
plugin is retained only as a diagnostic command; its runtime speed is not
release-acceptable.

Build or incrementally reuse the current development fallback archive:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 build-aot-fallback `
  --prepared-directory <directory-returned-by-prepare> `
  --generated-directory <audited-generated-cpp-directory> `
  --llvm-root <pinned-llvm-root> `
  --nlohmann-include <verified-include-root> `
  --jobs 4
```

This lower-level command first proves that the generated sources match the same private
`code.bin`, ExHeader, structural IR and selected 12,422-function product. It
then compiles only missing content-addressed objects with optimized ThinLTO and
no debug payload. The normal end-user workflow invokes this cache through the
ABI-v2 plugin builder rather than asking for a generated directory manually.

The validated full-product run compiled 257 sources in 274 seconds with four
workers and produced a 213 MB ThinLTO archive, versus 558 MB for the previous
debug-bearing archive. An unchanged second invocation audited and reused all
257 objects in 3.7 seconds end to end (1.41 seconds inside the object backend).
The new archive then linked into `oot3d_game_module.dll`, passed the 120-frame
module smoke with the established PICA/audio/input/filesystem counters and
completed the 180-frame NRI/Vulkan vertical. Its first ThinLTO module link took
about 195 seconds; this cost is cacheable in development but is further
evidence that the final installer needs the direct emitter and dedicated
module linker rather than the general CMake graph.

Package a locally generated private module without rebuilding the runtime:

```powershell
.\scripts\triaevum\Invoke-TriAevumForge.ps1 package-module `
  --prepared-directory <directory-returned-by-prepare> `
  --native-image <path-to-local-game-module.dll> `
  --target-triple x86_64-pc-windows-msvc `
  --translator-identity <64-hex-build-identity>
```

Forge writes the TAM under `modules/` and records the active full identities in
`forge-state.json`. Repeating the command with identical inputs reuses the
verified module. Different translator or native-image identities coexist; no
existing private module is overwritten.

By default output goes under `%LOCALAPPDATA%\TriAevum\titles`. `content.tap`
contains local absolute paths and hashes, is marked non-redistributable and
must never be uploaded. Use `--rehash` when source metadata cannot be trusted.

## Current runtime boundary

The private companion supplies whole-AOT title execution while the public
mature host owns CTR scheduling, guest memory, portable state, PICA/NRI,
audio, input and the F1/TopScreen runtime. The compatibility TAM remains in the
private cache, but normal play loads the ABI-v2 DLL and launch profile directly.
This avoids duplicating mature renderer and UI behavior in the incomplete
module host.

Forge prepares and audits structural IR from explicit private inputs, emits
the same generated-C++ whole-AOT used by the performance oracle, and activates
the NRI/TopScreen launch profile. Detailed evidence is in
`TRIAEVUM_FORGE_WHOLE_AOT_V2.md`.

The normal developer loop and the final clean-machine installation flow are
specified separately in `TRIAEVUM_DEVELOPMENT_AND_RELEASE_WORKFLOWS.md`.
