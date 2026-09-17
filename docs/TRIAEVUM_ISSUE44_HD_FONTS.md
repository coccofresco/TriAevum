# Issue 44: High-Resolution Native Fonts

Research: 2026-09-17. Status: cause and implementation route identified;
no runtime fix or issue closure claimed.

## Findings

[Issue 44](https://github.com/coccofresco/TriAevum/issues/44) requests higher
resolution fonts alongside HD textures; it does not itself identify the cause.

Henriko Magnifico explicitly credits RTG/M-1 for enabling 4x dialogue text in
the [OoT3D 4K 5.0 changelog](https://www.henrikomagnifico.com/post/zelda-oot-3d-4k-texture-pack-5-0-the-single-screen-update-changelog).
Thus the demonstrated solution is not simply a new Azahar PNG naming scheme:
it depends on the Single Screen Experience/TopScreen font capability.

The [N64-style HD font author's instructions](https://gamebanana.com/mods/713226)
identify 64x64 glyph support, QBF replacements under `load/mods/<title>/romfs/`,
and credit M-1/rlgcarrot for dynamic font loading and conversion tooling.
Henriko supplies artwork using that capability. Do not conflate these credits.

The official [Citra custom-texture documentation](https://citra.azahar-emu.org/help/feature/custom-textures/)
describes whole-upload texture hashing and replacement. Our matching mechanism
does the same. Generated text atlases change with their content; a stable font
resource is not equivalent to every texture subsequently assembled from it.
This explains why ordinary static texture packs are not a general font solution;
it is not a claim that Azahar cannot display HD fonts.

## Verified Local Gap

- `tools/triaevum_release/topscreen_assets.py` invokes the CTXB-only builder
  `tools/oot3d/decomp_support/scripts/build_topscreen_texture_override_pack.py`.
- That builder imports two localized menu textures plus `custom_menu00.ctxb`
  and `custom_font00.ctxb`. The latter is a TopScreen menu atlas, **not** the
  native dialogue font. No QBF is imported by this path.
- The pinned official `topscreen211.zip`, SHA256
  `e0c143c872ccf4ad72033caab768753b147a4033d260a701204a63ee6bc16df4`,
  already includes two 427,592-byte QBF files:
  `TopScreen2.1.1/EUR/4K Textures/load/mods/0004000000033600/romfs/message/eu/ltn16.qbf`
  and the USA equivalent under `0004000000033500/romfs/message/us/ltn16.qbf`.
- Both inspected headers start with `QBF1`; bytes +0x0c/+0x0d/+0x0e are
  `04/40/40`. The native consumers below establish these as bit depth and
  glyph width/height (4 bpp, 64x64), not guessed texture dimensions.
- `runtime/three_ds_recomp/src/fast/oot3d/azahar_texture_pack.cpp`,
  `ComputeAzaharTextureHash`, hashes native upload bytes (or legacy detiled
  data). It has no font/glyph identity or text-layout contract.

## Native Evidence Already Available

Read-only `I:/oot3decomp/ghidra_export/decompiled/`:

| Native function | Relevant evidence |
| --- | --- |
| `00419e18` / `oot3d_load_message_resources` | Selects localized QM/QBF resources, including EU/US `ltn16.qbf` and JP fonts |
| `0046b114` / `oot3d_init_sys_message_font` | Separate system-font initialization; must not assume dialogue coverage also fixes system UI |
| `002da7b8` | Reads QBF +0x0c (bit depth, used by allocation calculation) |
| `002da7d8`, `002da7c8` | Read QBF +0x0d/+0x0e (glyph dimensions) |
| `002da7e8` | Computes atlas extent from glyph dimensions in mode 1, allocates/resizes backing store, publishes dimensions |
| `002b7234` | Emits glyph quads and normalized atlas UVs; geometry and source texel dimensions are coupled |
| `0044ce90` | One text constructor supplies fixed `0x100` capacities/extents; demonstrates why replacing QBF alone is insufficient |

The existing native song-text owner is another consumer to preserve:
`oot3d_top_screen_ocarina_text_runtime.cpp`; see
[localized text lifecycle](TRIAEVUM_TOPSCREEN_OCARINA_TEXT.md). It captures
foreground/shadow atlas layers before native destruction.

Independent source corroboration: [OTPR26/OOT3DHud patches](https://github.com/OTPR26/OOT3DHud/blob/99e8ef0d7bd888b6a12220900186727b7b6c3052/src/patches.s)
and [EUR patch map](https://github.com/OTPR26/OOT3DHud/blob/99e8ef0d7bd888b6a12220900186727b7b6c3052/oot_e.ld).
That implementation uses **32x32**, not TopScreen's inspected 64x64 payload:
atlas sizing, command capacity, quad geometry, line advances and text measurement
are all adjusted. Its half-height, baseline and width constants are specific to
that font treatment and must not be copied as a universal solution.
Source and artwork have different licensing scopes; no donor code/assets were
copied into this repository during research.

## Recommended Implementation

1. Add a bounded QBF parser and explicit font-pack record in the OOT3D asset
   adapter: glyph coverage, encoding, bit depth, source cell dimensions and
   separate logical metrics. Validate offsets, counts and allocation limits.
2. Extend the verified local TopScreen import to include its QBF resources,
   version the receipt, and expose user font overrides separately from ordinary
   Azahar texture replacement. Keep all mod payloads outside public packages.
3. Adapt native font/text owners, not the shared PICA shader: retain original
   message parsing, control codes, timing, layout and measurement, while building
   high-resolution glyph coverage and adequate atlas allocations. Derive density
   from native/replacement metadata; do not globally divide font measurements.
4. Publish high-resolution surfaces with logical glyph geometry and correct UVs
   through the existing native composition path. Preserve foreground/shadow,
   alpha coverage, clipping and centered UI aspect. NRI remains title-neutral.
5. Cache by font identity, language and glyph/page generation. Invalidate at font
   reload, language change and quickload. Do not rebuild atlases every frame or
   key them solely by transient guest addresses.

Prefer completing the native owner's font-density contract over intercepting
finished low-resolution textures by hash, OCR, per-message texture dumps, global
sharpening, or importing the donor's executable patch into the whole-AOT image.
A renderer-side glyph atlas can remain an implementation option if the owner
can publish exact glyph provenance; it must not infer characters from pixels.

## Acceptance Before Closing 44

- Compare native/HD text bounds, advances, line wrapping, clipping, shadows,
  colors, choice prompts and reveal timing against unmodified execution.
- Exercise accented EU text and US text, song names, changing dialogue pages,
  system/save menus, 4:3/16:9, TopScreen on/off and savestate restore.
- Validate malformed QBF rejection and native fallback without partial state.
- Measure atlas rebuild frequency/memory, and capture actual NRI framebuffers
  on Windows/Linux. Do not treat loading a QBF or a parser unit test as proof
  of correctly rendered HD dialogue.
- Verify the original TopScreen font-consumer changes against the existing mod
  exports before implementation; the public 32x32 donor is corroboration, not
  authoritative evidence for all 64x64 behavior. Other scripts/languages and
  system fonts require their own coverage, not an assumed Latin-only fix.
