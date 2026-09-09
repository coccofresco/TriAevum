# Public UI Source Repair

## Diagnosis

The Linux contributor's report is correct. Published sources omitted a required
UI dependency, not an optional feature. `release_policy.json` excluded
`tools/oot3d/decomp_support/evidence`, while CMake still referenced the pinned
`oot3d_ui` directory there. When its C++ files were missing, CMake created an
empty `INTERFACE` library, but TopScreen, the HUD presenter and the game still
included its headers. A successful developer build with private files present
did not establish that the published source was self-contained. The missing
dependency is platform-independent; it was not a GCC 16 portability defect.

## Repair

- Publish the required 36 headers and 26 C++ files as the title-owned module
  `tools/oot3d/ui_contract/oot3d_ui`, with an explicit source/provenance manifest.
- Preserve the already-used revision and `HorseStamina` extension. Do not
  resynchronize gameplay or change UI behavior to solve a packaging failure.
- Obtain include paths transitively from `oot3d_native_ui_contract`. Remove
  the external evidence-root option, glob and silent empty-library fallback.
- Fail during CMake configuration if an inventoried header/source is missing.
- Require the same inventory when producing and auditing corresponding-source
  ZIPs. Keep all existing private-evidence/ROM/asset filters intact.
- Build real UI consumers from the **exported ZIP**, not only the checkout,
  in CI with GCC, Clang and Windows MSVC. This narrow check needs no ROM, GPU,
  private snapshot or title recompilation.

The imported hashes and upstream revision are in the module's
`SOURCE_MANIFEST.json`; they document provenance, not a claim that reconstructed
UI code is title-neutral or a new license for original-game material.

## Verification

On the physical Linux host, GCC 16.2.1 and Clang 22.1.8 compile the full contract
and three real consumers. All three tests pass with each compiler. The existing
Clang NRI/Vulkan runtime and game-module build also links successfully after
switching to the public module (76 incremental build actions, no title rebuild).
The Forge/release suite runs 228 tests on Windows with two existing skips and
no failures. Tests deliberately remove a UI header from an otherwise correctly
hashed package and check that release auditing rejects it; filtering a required
source before ZIP creation is rejected as well.

The local Visual Studio installation lacks `cl.exe`; native MSVC compilation
is covered by the new CI job, not claimed as a completed local test. Building
the whole runtime with GCC remains separate from the contributor's reported
444 successful compilation steps and from our isolated GCC UI verification.

## Reproduce

From a clean public checkout, create the same filtered source archive shipped
to users, extract it, then compile the UI consumers:

```sh
python3 tools/triaevum_release/source_archive.py --repository . \
  --source-commit "$(git rev-parse HEAD)" --output /tmp/TriAevum-source.zip
python3 -m zipfile -e /tmp/TriAevum-source.zip /tmp/triaevum-public-source
cmake -S /tmp/triaevum-public-source/tools/oot3d/ui_contract \
  -B /tmp/triaevum-ui-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/triaevum-ui-build --parallel 3
ctest --test-dir /tmp/triaevum-ui-build --output-on-failure
```

The exported source does not require `OOT3D_NATIVE_UI_EVIDENCE_ROOT` or another
Zelda3drecomp checkout. Existing old cache entries for that variable are unused.
Do not alter historical source receipts to pretend older archives included
these files: corrected archives must identify the corrected commit.

## Steam Deck Boundary

This closes the required UI-source omission for the Linux/Steam Deck work.
It does not finish portable dependency packaging, Forge window visibility,
SSSR, graphics performance or device qualification. Use the checked-in module
in subsequent Steam-compatible builds and repeat the public-source archive
test on that toolchain. See `TRIAEVUM_LINUX_PORT.md` and
`TRIAEVUM_LINUX_FORGE.md` for the remaining product tests.
