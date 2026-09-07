# OOT3D Native C/C++ Corpus Promotion

## Purpose

The room compilation unit identifies the native OOT3D function closure required by
a scene route. `promote-native-corpus` turns the generated Ghidra evidence for that
closure into a deterministic, inspectable source package. It does not substitute
N64 implementations and it does not generate gameplay stubs for missing bodies.

This is the boundary between decompilation evidence and maintained host C/C++:

1. The room compilation unit selects required functions and consumer roots.
2. Function inventory and maintained data symbols are read from an exact
   `Zelda3drecomp` Git revision.
3. Decompiled bodies are selected by native address, preferring typed corpus
   entries.
4. Exact `FUN_XXXXXXXX` and known `DAT_XXXXXXXX` names are normalized from the
   maintained inventories.
5. One source file is emitted per native function so later build and ABI failures
   remain isolated.
6. Missing bodies and unresolved hazards remain explicit in `manifest.json` and
   `report.md`.

## Generated Corpus Provenance

`Zelda3drecomp/build/analysis/decomp_batches` is generated and ignored by Git. If a
future revision commits that corpus, all inputs are read with `git archive`. For the
current layout, promotion enforces all of the following:

- the checkout HEAD must equal the requested revision;
- tracked inventories and symbol tables are still read only from `git archive`;
- the sidecar directory must be ignored and contain no tracked files;
- every corpus file and selected function body contributes to recorded SHA-256
  digests;
- tracked worktree modifications are never consumed.

The sidecar digest makes the copied package immutable and comparable. It does not
claim that the generated corpus itself was committed to `Zelda3drecomp`.

## Kokiri Initial Route

Run from `I:\oot3dre-vulkan`:

```powershell
$env:PYTHONPATH='tools/oot3d/oot3d_asset_tool/src'
python -m oot3d_asset_tool promote-native-corpus `
  --room-unit 'I:\oot3dre_work\room_compilation_units\scene_entry_SCENE_ENTRY_KOKIRI_FOREST_FROM_LINKS_HOUSE_KOKIRI_FOREST_INITIAL_CHILD_DAY.json' `
  --zelda3drecomp-root 'I:\Zelda3drecomp' `
  --revision 607aaa60e1e94caaeaaa524c9d6d120fdd265701 `
  --output 'I:\oot3dre_work\native_corpus\kokiri_initial_607aaa6' `
  --replace
```

The package contains:

- `manifest.json`: revision, corpus digest, room identity, coverage, hazards,
  function metadata, call edges, body hashes, and output hashes;
- `report.md`: concise coverage and blocker queue;
- `include/oot3d_native_corpus_forward.hpp`: analysis-only declarations;
- `include/oot3d_native_layouts.hpp`: evidence-selected packed layouts and guest
  pointer storage;
- `src/<module>/<address>_<name>.cpp`: copied and symbol-normalized OOT3D bodies.

## Current Result

For the initial child/day Kokiri route at revision `607aaa6`:

- 879 profile references resolve to 526 unique native functions;
- 500 bodies are promoted, including all 98 unique consumer roots;
- 172,260 of 173,692 native function bytes have bodies;
- 499 promoted bodies come from typed corpus entries;
- 26 bodies, totaling 1,432 bytes, remain missing;
- 95 bodies have none of the currently listed Ghidra hazards;
- 8 extracted candidates still require function-boundary/signature recovery;
- 13 bodies still contain unrecovered indirect control flow.

The generated ABI scaffold now consumes 76 versioned field catalogs plus all 432
native ActorInit prefix records. It emits 111 partial structures with 600 named
fields; only `Oot3dAnimationBinding` and `Oot3dZarArchivePrefix` remain forward-only.
Overlapping recovered views are represented explicitly as anonymous-union overlays,
and embedded pointers use the four-byte `GuestPtr32<T>` representation.

The package also imports 363 unambiguous signatures for services actually called by
the closure. `Audio_PlaySoundAtPosition` remains intentionally unresolved because
two reviewed catalogs assign incompatible ABIs.

A strict C++17 syntax probe with
`C:\ProgramData\mingw64\mingw64\bin\g++.exe` now compiles 65 of the 95 cleanest
bodies, up from 9 before layout and service promotion. Native `(int)pointer`
decompiler idioms are normalized only for variables or member addresses proven to
be pointers, using host-width address arithmetic. Integers loaded from guest memory
are not silently treated as host pointers.

The promoted decompiler corpus remains an analysis package rather than the executable
runtime. Executable coverage now comes from the pinned Zelda3drecomp A32 backend and
its deterministic C++ AOT generator, copied from immutable evidence snapshot
`850483fb59713e87`. The generated image covers all 794,890 proved executable slots,
with zero delegated or fallback slots; 20,214 literal-data slots and 38 words without
proved reachability remain outside executable coverage by design.

`oot3d_native_game` links that AOT image through an isolated CMake target. Its strict
bootstrap verifies that RCU, promoted corpus and A32 source revision agree. The Kokiri
validation currently resolves all 526 required closure entries in the AOT registry.
`tools/oot3d/build_fast_dev.ps1` runs the pinned generator before relevant native
targets and reconfigures only if the isolated AOT target is absent, so a clean build
cannot silently omit executable coverage. An unchanged preflight takes less than one
second and does not invalidate AOT objects.
This proves code availability, not yet complete gameplay execution: the remaining
runtime boundary is guest memory, service and callback state transport. Local behavior
reconstructions must be replaced incrementally only after that transport can execute
and validate the corresponding native entry.
