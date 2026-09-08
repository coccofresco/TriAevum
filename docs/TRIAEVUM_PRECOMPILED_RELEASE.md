# Precompiled release contract

Adopted 2026-09-05 at the maintainer's explicit request, following the distribution
model of static Xbox 360 recomp ports. This supersedes earlier requirements to
compile title code on the end user's machine. It changes distribution, not the
runtime execution backend, game timing, graphics or save format.

## User contract

- One GUI: `TriAevumForge.exe`, one input: a supported decrypted `.3ds`/`.cci` ROM.
- Forge verifies/extracts the ROM and activates a shipped optimized title DLL.
- No C++ compiler, linker, SDK, IR generation or compiler cache is needed.
- Title preparation needs no network request. TopScreen's default texture import
  downloads its official archive unless a verified local archive/pack exists;
  see `TRIAEVUM_TOPSCREEN_INSTALLATION.md` for offline setup. Unsupported ROM
  revisions or missing/corrupt modules produce an error, never an automatic build.
- Data, saves and user settings remain under `data/`; existing valid settings
  are preserved. NRI/Vulkan, TopScreen and F1 retain their mature implementations.
- Native simulation stays 30 Hz with the existing default x2 presentation.
- Fresh graphics configs also select the title-owned 1.10x FOV, toon/outline
  and grass snapshot described in `TRIAEVUM_GRAPHICS_DEFAULTS_AND_GRASS_PERFORMANCE.md`.
  This is not a change to the shared renderer's Authentic preset.

## Package boundary

Ship the neutral runtime, explicitly identified translated title logic, Forge,
revision recipes, notices and corresponding source. Do not describe this package
as containing no title code. Original ROMs, `code.bin`, ExHeader, RomFS, extracted
assets, mod payloads, keys, SDKs, captures and saves are still excluded.

`recipes/precompiled-titles.json` binds every module to exact code/ExHeader/RomFS
hashes and sizes, ABI/target, translator identity, runtime and neutral-module
hashes. It also identifies the translated-source archive and the source snapshot
used for the title build. The publisher checks the real DLL with the real runtime.
Post-alpha.1b packages can additionally bind an explicitly qualified
[content family](TRIAEVUM_CONTENT_FAMILY_IMPORT.md): execution code stays exact,
while equivalent ExHeader packaging and logical RomFS contents may differ in
their complete-file identities. Prepared files retain their actual hashes and
are revalidated against the module-bound family before activation.
Hash validation is integrity checking, not cryptographic publisher authentication.

The user requested this distribution policy after discussing the Xbox 360 model.
The project's license does not grant or relicense original-game rights. Existing
donor notices remain; earlier legal approval is not expanded into a blanket legal
guarantee for every possible artifact.

## Implementation owners

- `precompiled_title_layout.py`: publisher promotion of a completed optimized
  build, generated source verification and source/catalog packaging.
- `precompiled_titles.py`: strict revision selection and transactional activation.
- `forge_gui.py` / `install_worker.py`: ROM-only user flow, with no compiler fallback.
- `prepare_release.py`: developer build/package workflow, not user preparation.
- `audit_release.py`: explicit precompiled roles; rejects uncatalogued modules,
  ROM/content contamination and compiler payloads in the user package.
- Existing `forge build-title`: developer-only optimized title generation.

## Reproduce a release

Build the title once using the documented developer `build-title` path from the
user-owned original revision. Preserve `whole-aot-plugin.json`, its adjacent DLL,
the corresponding generated C++ manifest/files and the exact source snapshot
used for that build. Never copy an entire translator cache into a release.

Run `python -m tools.triaevum_release.prepare_release` with `--build-dir`,
`--cmake`, `--include`, `--work`, `--output`, `--version`, and:

```
--title-build <whole-aot-plugin.json>
--title-sources <generated-C++-manifest.json>
--title-build-source <title-build-TriAevum-source.zip>
--recipe oot3d-eur-project-baseline-16a6b0aa
```

Also supply `--vc-redist-dir` pointing to the licensed publisher's
`VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` directory. The package
includes shaderc's three Visual C++ runtime DLLs app-locally, SDL controller
mappings and their notices. The release audit rejects omission of these
dependencies; an SDK installed on the developer PC must not hide missing
user dependencies. SDL2 itself is statically linked by the Windows build.

The runtime's source archive follows its own current commit; the title build
snapshot follows its build commit. These can differ and must not be conflated.
Generated C++ is included separately, so its hashes can be checked and changes
made without regenerating it. To rebuild that code directly, use the paired
source's `build_generated_cpp_archive` in `whole_aot_object_cache.py`, support
library CMake target and wrapper/link flags in `whole_aot_plugin_backend.py`.
The original `build-title` developer path remains the end-to-end rebuild recipe
from ROM. Byte-identical builds across different toolchains are not promised.

## Qualification

Run release unit tests and the allowlist audit; install from a fresh copied
package using the frozen worker and a ROM, without copying any SDK or build cache.
Record total extraction-to-ready wall time separately from game boot and FPS.
Boot the resulting profile and capture the framebuffer; inspect TopScreen,
renderer identity and interpolation counters. Keep performance claims separate
from visual/audio/manual validation. No modest-PC time target is claimed until
measured on one. `--candidate` remains an explicitly unqualified test artifact.

## First measured end-to-end result

Implementation commit: `1d4e95cc9`, branch `release/triaevum-precompiled`.
Candidate: `I:/TriAevum-0.6.0-candidate-r1`, 37 files / 180,612,481 bytes,
allowlist audit passed. Runtime and Forge correspond to that commit; the title
DLL is reused unchanged from the earlier full ROM-build proof. No game asset,
compiler executable, SDK generation or translator object cache is packaged.

On this 12-logical-CPU Windows host, the frozen installation worker imported
`I:/Zelda3drecomp/oot3d.cci` into a fresh copy of the candidate:

- Worker process start to exit: **20.905 seconds**.
- Import/verification/activation inside the worker: **17.016 seconds**.
- Compiled objects: **0**; no `translator-cache` or `toolchain` directory created.
- Runtime: successful 30-second scheduled run, exit 0; framebuffer inspected,
  showing Link/Epona, terrain, sky and moon in the title intro.
- TopScreen active, NRI/Vulkan, visual interpolation x2. Measured presentation
  59.226 FPS over 1,713 frames / 28.923 seconds after warmup, with VSync/pacing
  enabled. This is a functional paced-run result, not maximum throughput.
- Both processes were closed by the harness; no test executable left running.

Private evidence: `I:/oot3dre_work/triaevum-precompiled-r1-proof/` contains
`qualification.json`, `forge.stdout.log`, `runtime.json`, `framebuffer.bmp` and
the prepared `installation/`. Never include this proof directory in a release.

Package manifest SHA-256:
`ff306c4492bf9a94dd8d5d1fdc23406b3014b7216c313c35e950cb38b6b632fa`.
Qualified title DLL SHA-256 (unchanged from the locally compiled proof):
`106aa7a6b1be71b2f2cee9c0f4baf2a3f0d43a3c4a1443e84ff365837c536549`.

147 release tests passed (including the native CMake probe), 13.025 seconds.
The runtime code and title DLL are not rewritten for this distribution change.
This machine has development software installed: an empty install directory is
not a clean-Windows VM claim. Modest-PC timing, clean-machine qualification and
new interactive F1/audio/save validation remain explicitly unclaimed.
