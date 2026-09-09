# MMJ Shader Corpus Review

Date: 2026-09-09. Scope: inspect and test the four user-supplied OOT3D MMJ
caches for the Forge shader-preparation work. No game, APK, renderer or public
release payload is changed by this review.

## Result

The four files are **two distinct text GLSL corpora**, not transferable PICA
register caches and not GPU driver binaries. Each USA/EUR pair is byte-identical.
All metadata counts were checked against parsed records; all references and
program stage links resolve with the expected shader types.

| Directory / metadata version | Bytes per region | Vertex | Geometry | Fragment | References | Linked programs |
|---|---:|---:|---:|---:|---:|---:|
| `cache/` / 8 | 251,627 | 1 | 1 | 48 | 55 | 48 |
| `cache/26/` / 26 | 158,715 | 1 | 1 | 45 | 51 | 45 |

There are 97 distinct stage/source pairs and 93 stage/source program triples
across both corpora. None of their source bodies match exactly across versions
after line-ending and surrounding-whitespace normalization. These are **source
variants, not proof of 97 distinct native PICA programs or additional game
coverage**. Different helper implementations and interfaces change source hashes.
The larger file is not evidence of proportionally broader coverage.

## Provenance

Repository: [weihuoya/citra](https://github.com/weihuoya/citra), pinned revision
`302b0c3bc4312bc96489c11584a97664894b343f`.

- [Version 8 USA corpus](https://github.com/weihuoya/citra/blob/302b0c3bc4312bc96489c11584a97664894b343f/cache/0004000000033500.shader)
  and its adjacent `.shader.meta`; EUR is `0004000000033600.shader`.
- [Version 26 USA corpus](https://github.com/weihuoya/citra/blob/302b0c3bc4312bc96489c11584a97664894b343f/cache/26/0004000000033500.shader)
  and its adjacent `.shader.meta`; EUR is `0004000000033600.shader`.

SHA-256, identical for USA/EUR within each row:

| Directory | Shader SHA-256 | Metadata SHA-256 |
|---|---|---|
| `cache/` | `9fe5260074e0f2efa2db0f398648e4e9e54af7e7e741f5fb60b0ee439d687862` | `e1a7ceb5c2390c3e215e9d864af0a46869ce6c29266c5edaef3befa81d762328` |
| `cache/26/` | `92ff025d80261ba523aca8403f3aa2fefd925e344c82a2bd1f92f4bfe9f2a41b` | `6bde189fd6bc1a59a24ffd7952651de90c143a279a6ba580414b0612609e2ed3` |

Credit: weihuoya/Citra MMJ and the Citra contributors for the reference corpus
and shader generator. No donor GLSL or downloaded cache is included in the
public source changes. The numeric metadata versions do not establish that the
two corpora share a configuration-hash ABI or indicate their chronological order.

## What The Format Actually Carries

The source files delimit records with `shader`, `reference` and `program` comment
markers. Shader records contain a GL stage enum, an opaque source identifier and
GLSL source. References relate opaque configuration IDs to source IDs. Programs
are vertex/geometry/fragment ID triples, **not complete graphics pipeline states**.

The inspected MMJ
[shader manager](https://github.com/weihuoya/citra/blob/302b0c3bc4312bc96489c11584a97664894b343f/src/video_core/renderer_opengl/gl_shader_manager.cpp)
hashes its `PicaVSConfig` / `PicaFSConfig` and generated source, and links bundles
of stages. Its checked-in persistence code uses a different binary `.cache`
format; it is not treated as the exact producer of these two text export versions.
The text markers and supplied metadata define this inspector's input boundary.

Missing from the text corpus:

- The original 768-register PICA snapshots and vertex program/swizzle words.
- Uniform values, textures, resident LUTs and per-draw resource bindings.
- Depth/stencil/blend/cull state, attachment formats and sample counts needed
  for complete NRI pipeline recipes.
- Proven mapping between MMJ configuration IDs and our canonical source/state
  identities. Neither a filename change nor an ID copy establishes this mapping.

## Important ABI And Fidelity Differences

Both corpora route the programmable vertex outputs through an emulator-generated
geometry stage. It supplies output-semantic mapping, quaternion hemisphere
correction, color clamping and clipping. It is **not evidence of an additional
original game geometry shader**. Preserve these requirements through our existing
PICA/NRI path rather than installing an extra donor geometry stage blindly.

The GLSL uses MMJ uniform blocks, sampler bindings and OpenGL depth conventions.
For example, version 8 reconstructs clip depth from OpenGL window depth before
applying its depth scale/offset. Version 26 changes binding qualifiers and uniform
field types/order. Successful compilation alone cannot validate host ABI or the
Vulkan coordinate/depth contract.

Version 8 also includes modified reciprocal/inverse-square-root helpers, and its
48 fragment sources define ShadowTexture helpers returning white. In this corpus
each helper name occurs only in its definition, with no additional call sites;
this is **not a demonstrated visible shadow defect in these 48 programs**. It is
evidence that importing the helper library as a general PICA implementation would
be unsafe. Version 26 differs again. The canonical frontend remains authoritative.

## Tests Performed

- Downloaded all eight files (four shaders plus four metadata), pinned their
  upstream revision, checked sizes/hashes and regional equality.
- Parsed stage records and metadata, checked reference targets, conflicting IDs,
  stage ordering and missing shaders with the new bounded stdlib-only inspector.
- Nine synthetic parser tests pass on Windows: versions 8/26, newline identity,
  metadata/count mismatch, malformed records, missing/wrong stage references,
  configuration/source collisions, count limits and helper observation reporting.
- On the Linux host, `glslangValidator -l` compiled and linked **48/48 + 45/45**
  complete program triples. The inspection harness prepends `#version 450 core`
  when absent; it does not rewrite donor shader bodies, resources or arithmetic.
  Duplicate regional input is compiled only once. No driver/GPU is involved.
- Compared normalized exact stage/source hashes against the existing 704-module
  Citra-derived inventory and the 79-module boot/title/Kokiri inventory:
  **zero exact matches**. This does not prove different rendering behavior or
  additional coverage; the generators and output ABIs differ.
- **Zero canonical modules added or enabled. No Vulkan/game equivalence test is
  claimed, and no performance improvement is attributed to this inspection.**

## Decision For Forge

Keep the MMJ files as a compact **reference corpus of linked programs**. They are
useful for checking shader interfaces and identifying the limited set of vertex
paths involved, but they must not be mistaken for a larger drop-in native seed.
In particular, 48/45 MMJ fragment sources are not directly comparable to the 352
canonical fragment sources generated from the 426 raw Citra configurations.

The highest-value implementation remains the existing plan:

1. Feed ROM-derived SHBIN programs through the existing native vertex frontend,
   preserving uniform/output metadata and NRI hooks.
2. Combine those with canonical fragments from raw PICA states and real pipeline
   recipes. Use the MMJ source/triple corpus for targeted comparisons after an
   explicit mapping is established, not as authoritative runtime identifiers.
3. Perform device-specific pipeline preparation with the same renderer/contracts
   as the game. Do not link all shader combinations indiscriminately or replace
   correct runtime output with foreign GLSL to inflate cache-hit statistics.

No user-facing Forge option was added for a format that cannot yet yield a
correct canonical seed. Existing transferable import and optional pack
preparation are unchanged. See [Forge shader preparation](TRIAEVUM_FORGE_SHADER_PREPARATION.md).

## Reproduce

Tool: `tools/oot3d/native_pica_frontend/inspect_mmj_shader_cache.py`.
The adjacent `.shader.meta` is required for each input. Reports contain donor
identifiers and remain private, like the downloaded sources.

```text
python -m unittest discover -s tools/oot3d/native_pica_frontend -p test_inspect_mmj_shader_cache.py
python tools/oot3d/native_pica_frontend/inspect_mmj_shader_cache.py --input PRIVATE/current/0004000000033500.shader --input PRIVATE/26/0004000000033500.shader --output PRIVATE/mmj-report.json --validator /usr/bin/glslangValidator
```

Local evidence: `I:/oot3dre_work/citra-cache-review/mmj/`, including all four
downloaded shader/metadata pairs, `inspection.json`, `validation.json`, and the
read-only MMJ source references under `source/`.
Linux evidence: `/home/xander/triaevum-android-build/mmj-validation.json`; inputs
under `current/` and `26/`. No game/title rebuild or Android APK install was needed.
