# TriAevum public runtime vertical

> Distribution update (2026-09-05): [Precompiled release contract](TRIAEVUM_PRECOMPILED_RELEASE.md)
> supersedes this document's user-side compilation and no-translated-title-code
> requirements. The material below remains developer/history context; users now
> import their ROM into a package containing precompiled title logic, without SDKs.

> Historical ABI-v1 pilot, not the current release host. Since the v2 migration
> use `oot3d_native_game` / `triaevum_public_runtime`, emitted as `TriAevum.exe`.
> The old target is `TriAevumModuleDiagnostic`. See
> [current product workflow](TRIAEVUM_PRODUCT_WORKFLOW.md).

## Verified boundary

The public executable is the CMake target `triaevum_oot3d_module_host`, emitted
as `TriAevum.exe`. It does not link `oot3d_game_module` or any user-derived
title image. It discovers a private Forge title through
the portable `data/active-title.json` produced beside the executable by Forge,
then resolves only contained module and content paths from `forge-state.json`
and loads the TAM through the stable module ABI. A platform user-data directory
remains the fallback when no portable title is installed.

Runtime resources are relative to the executable. Configuration, controls,
module extraction cache and active-title selection live under the user data
root. Explicit paths remain available for developer diagnostics but are not
required by an installed launch.

## 2026-09-02 validation

A package-shaped directory containing only `TriAevum.exe`,
`shaderc_shared.dll` and the title-neutral `resources/README.txt` was launched
outside the source and build trees. A frozen one-file `TriAevumForge.exe`
selected an already verified private title; the runtime then launched with
only `--data-root`, `--renderer nri`, a frame limit and a framebuffer capture.

The 120-frame NRI/Vulkan run completed with:

- 53 submitted PICA batches and 708 draws;
- 53 display transfers and 112 retained scanout presentations;
- 143 completion publications;
- 409 audio submissions and 65,440 accepted PCM frames;
- 120 host input polls and 120 guest input reads;
- 63 private filesystem reads totaling 19,269,907 bytes;
- a non-black top framebuffer and a valid captured night scene.

The same runtime also passed with an empty explicit resource root, proving that
no source-tree shader or title asset is read at launch.

## Frozen Forge validation

`tools/triaevum_release/build_forge_binary.py` builds the one-file executable
from an allowlist of Forge modules, structural translator metadata and parser
sources. It does not bundle a ROM, title bytes, generated C++, native objects or
a private module.

From outside the repository the frozen application:

1. passed `doctor` with the supported revision recipe;
2. reconstructed and audited structural IR containing 12,422 functions,
   161,341 blocks and 918,019 unique instructions with zero residual A32
   entries;
3. compiled the verified title directly into 129 LLVM/COFF objects and a
   24,982,016-byte private title companion;
4. packaged and activated TAM 1.1 with the generic public game module and
   private companion;
5. completed the fresh dump-to-title path in under 95 seconds and reused the
   identical installed result in 1.9 seconds on the second invocation.

The isolated runtime then completed 600 NRI/Vulkan frames from that generated
title with 8,231 draws, 327,200 accepted audio frames and a valid open-title
framebuffer capture. See `TRIAEVUM_DIRECT_AOT_RELEASE_VERTICAL.md`.

## Corresponding-source and package audit

The deterministic source builder records the exact root/submodule commits,
file inventory, exclusions and source hash inside each source archive. The
package manifest similarly records every public file, role, size and SHA-256.
The independent public-package audit rejects unknown, private, title-derived
or proprietary artifacts and is a hard prerequisite of packaging.
