# TopScreen 2.1.1: Ocarina Presentation Owner

Date: 2026-09-10. Follow-up to
[refresh and assignment work](TRIAEVUM_TOPSCREEN_REFRESH_AND_OCARINA.md).

## Delivered Surface

The missing **song guide** now runs in the game, not just a geometry test:
staff, title band, learned-song note sequences, colored song markers and D-pad
navigation. B returns to normal gameplay and removes this presentation.
The unknown-song marker is reconstructed and unit-tested; its visual path has
not yet been qualified using a partially learned original save.

For the current status, see [ocarina and mapped actions](TRIAEVUM_TOPSCREEN_OCARINA_AND_ACTIONS.md):
the guide toggle is implemented and native recognition has been verified in
game. The remaining paragraphs record the scope at this earlier checkpoint.

This was **not yet complete ocarina parity**. The follow-up
[native localized song-text integration](TRIAEVUM_TOPSCREEN_OCARINA_TEXT.md)
fills the previously empty title band. Native free-play instructions, the live
history of performed notes, recognition/learning transitions and the original
guide-mode toggle remain separate work. Do not mistake the displayed reference
sequence for performed-note feedback.

## Root Causes And Repair

The old `ReadTopScreenWorldMapGeometry` was not a world-map presenter. It read
ocarina arrays but required an open pause and was called only for `Map`.
In the real free-play checkpoint, pause is zero and the ocarina page is 12.
Its band indices, shared color application and host UV conversion also differed
from the original producer.

The replacement is `tools/oot3d/ui_topscreen/oot3d_top_screen_ocarina.{h,cpp}`:

- Explicit native scene/result/page/transition ownership, independent of pause
  observations. The bridge publishes it through `TouchControls`, not `Map`.
- Slot 5 is the native `OcarinaPage` resource; slot 2 supplies navigation arrows.
  No texture-size classifier, lower framebuffer promotion or backend workaround.
- Six bands read indices **0,1,2,35,36,37**. RGB is white for bands and notes;
  only the two song markers and unknown marker use the selected song color.
- Generated host primitives use top-left atlas coordinates. Native PICA's V
  inversion is not applied a second time in this host UI path.
- The local browser follows the mod's twelve-song order, including unlearned
  entries. Direction changes act immediately, then repeat at 12/5 native updates.
  It does not write native cursor/quest state or issue synthetic touch gestures.
- Left/right ownership prevents simultaneous gameplay item shortcuts while
  browsing. Pending edges survive refresh-to-native-update gaps, including a
  release/repress between updates. Held repeat does not depend on presentation FPS.
- Quickload resets the local browser. No save format or guest save data changes.

The general textured quad type remains shared; ocarina-specific navigation and
tests were moved out of the large mod-profile file. NRI, Vulkan, shader planning
and the platform input backends are unchanged.

## Primary Evidence

Same official 2.1.1 EUR payload and hashes as the preceding report, under the
private `I:/oot3dre_work/topscreen-2.1.1-reference/` directory. Existing Ghidra
exports were checked against focused ARM/constant-pool disassembly.

| Owner/data | Contract |
| --- | --- |
| `0x005C9ED8` | Suppression predicate, **not** positive ocarina visibility |
| `0x005D2078` | Bands, selected-song notes, tint/alpha and unknown marker |
| `0x005D3D30` | Local selection, wrap, direction priority and 12/5 repeat |
| `0x005CBC54`, `0x005CA7B8` | Navigation arrows, slot 2, centers (50,204)/(350,204) |
| `0x005E21E8` in mod | Local selected song, initial value -2; not a game cursor |
| `0x005093F8`, `0x0050941C` | Native page state and transition |
| `0x0050944C`, `0x005097AC` | Native position and size arrays, vec2 stride 8 |
| `0x00509B0C`, `0x00509E6C` | UV extent and origin arrays |
| `0x004D53C8`, `0x004D541C` | Twelve note counts and sequence pointers |
| `0x0050A3B0`, `0x0053C9D4`, `0x00587A14` | Flag identifiers, masks and saved quest flags |
| `0x0050A1CC`, `0x0050A1E0` | Five note Y positions and atlas X coordinates |

Flag identifiers in the live fixture are 90..101. They index a mask table;
they are **not bit positions limited to 0..31**. Bounds checks use address
overflow and mapped-memory reads, not an invented enum range.
The unknown marker comes from literals `0x005D2720..0x005D273C`: destination
(173,26), extent (54,24), atlas (406,484), alpha .85, in the mod's 512-square
ItemIcons atlas (native shared slot 10, corrected 2026-09-11; the previous
description incorrectly named the custom menu atlas). Layout and timing constants above are recovered mod policy, not
scene-specific adjustments.

## PR #18 Assessment And Credit

[PR #18](https://github.com/coccofresco/TriAevum/pull/18), by
[999sian](https://github.com/999sian), Git author `sian <sian@localhost>`;
reviewed head `3655e285f4c53733b9e486a9434c073dba2d5954`, ocarina commits
`e3eca08` and `3655e28`.

The contribution supplies useful in-game evidence, the missing UI scope and
the importance of reading live rather than stale geometry. Its song-guide and
input-isolation intent is adapted here through native owners. It is **not a
wholesale merge or a claim that its author wrote this replacement module**.
The PR branch also contains unrelated stacked changes.

Not adopted: classifying lower-screen draws by texture dimensions/index count,
unchecked vertex copying, the suppression-predicate rewrite, banner offset
-196, or cycling twelve synthetic tile taps to skip unlearned songs. Official
code instead uses offset -182 and a local selection that includes unlearned
songs. Native quads 39..44 alone did not establish performed-note coverage in
our live checkpoint; do not promote them based only on the PR's classification.

## Verification And Continuation

Linux NRI/Vulkan, native 30 Hz without interpolation, isolated copies of the
original populated save. Private evidence:
`/home/xander/triaevum-pipeline-live-proof/ocarina-owner-final/` and
`ocarina-regression-{even,odd,title}/`. The final module/test extraction and
release/repress fix were rerun in `ocarina-owner-module-final/`: all ten
ocarina captures match the preceding implementation byte-for-byte.

- 400-presentation ocarina run: 350 active guide frames, 4,000 primitives,
  zero owner-read failures. Framebuffer checks show two different selected
  sequences/colors, then the ordinary HUD after B.
- Twelve inventory captures plus six intro captures match their preceding
  baseline byte-for-byte; those runs emit zero ocarina primitives.
- Paired 400-update assignment runs still deliver 3 ZR / 2 ZL edges and
  2 starts / 2 completions each, with identical getter/selection counters.
- Dedicated ocarina tests cover native gating, nonzero high flag identifiers,
  correct source indices/UVs/colors, unlearned suppression, navigation/repeat,
  reset and guest-memory nonmutation. Cadence tests cover short D-pad taps.

These are Linux tests. Windows and Android executables were not rebuilt or
visually qualified in this tranche. No new Azahar capture or completed-song
comparison is claimed. No ROM, save, payload or framebuffer is published.

Now integrated in the follow-up: original mod `0x005D4A8C` creates localized text via native
`0x00313CE0(0x4C)` / `0x002F57F0`, using message table `0x004D5480` + `0x9AD`;
`0x005D2078` draws it via `0x002FB944`. The follow-up documents the explicit
lifecycle, GPU waits, immutable snapshot and cleanup/reset.
The native ocarina submit `0x00425930` (draw `0x0042676C`) owns globals `0x005093E4..0x005093F4`:
model descriptor, model instance, quad renderer, cursor helper and text owner.
The quad renderer is at global **+8**, not +0/+4. These pointers are leads for
the remaining prompt/performed-note path, not a claim it is already qualified.
