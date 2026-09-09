# OOT3D UI Contract

This is a required, checked-in source dependency of the TriAevum runtime,
TopScreen and the alternate HUD presenter. It is not an optional evidence pack.
Builds must not fetch another repository or require a private snapshot.

The 36 headers and 26 C++ translation units were copied from the already-used
Zelda3drecomp snapshot `849140697187b895`, revision
`2cec5ef08305fbe1e4fe00efaf3f2467ff5228b1`. All files match that snapshot except
`ui_primitives.h`: TriAevum commit `0ac5109b4bcc8f0a6fb973dd2f87f28116d1245f`
added `UiPrimitiveRole::HorseStamina`. This extension is preserved. No new
decompilation, semantic resynchronization or UI behavior change is made here.

`SOURCE_MANIFEST.json` lists the required source files and records both snapshot
and imported SHA-256 hashes. Those hashes document the import, not an ongoing
prohibition on source edits. Register added/removed files in the manifest;
record subsequent code changes in Git. CMake and the source-package audit both
consume its file inventory, so a missing header cannot silently become an
empty interface library.

The module contains title-specific typed contracts, reconstructed UI behavior,
guest layout descriptions and semantic resource tables. It is not title-neutral
code or a copy of the game's UI textures. Existing comments and generator
references are preserved as provenance; regenerating these files is not a build
prerequisite. No ROM, executable image, asset, analysis dump or other snapshot
directory is included. The root `LICENSE_SCOPE.md` remains authoritative;
publishing these sources does not grant original-game rights.

## Standalone Check

```sh
cmake -S tools/oot3d/ui_contract -B build/ui-contract -DCMAKE_BUILD_TYPE=Release
cmake --build build/ui-contract --config Release --parallel 3
ctest --test-dir build/ui-contract -C Release --output-on-failure
```

This compiles the real contract, HUD and TopScreen hint consumers from public
files alone. It does not certify full game rendering or Steam Deck compatibility.
