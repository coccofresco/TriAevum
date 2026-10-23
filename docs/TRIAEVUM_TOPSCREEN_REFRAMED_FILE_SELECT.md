# TopScreen File Selection after Ocarina Reframed

TopScreen's file selection follows Ocarina Reframed 2.0.2 (OTPR26,
GPL-3.0-or-later; credited in `THIRD_PARTY_NOTICES.md`). The design is
Reframed issue #16: the title logo top-left, the file interface on the left,
Link's pedestal on the right with the caption box below it. It is
reimplemented as typed host code in
`tools/oot3d/native_game_runtime/oot3d_top_screen_reframed_file_select.{h,cpp}`;
no Reframed code is copied.

## Ownership

`TopScreenReframedFileSelect` owns the file selection while it is active:
`SaveContext.game_mode` (`0x00588E3C`) is 2 and the file board state
(`0x00504FB0`) is nonzero. Draw sites also require the upper view pass
(`0x00300328` called from `0x00419524`). Outside that gate every site runs
natively and the SingleScreen 3.1 port is unchanged.

While it is active, the 3.1 frontend relocation, the route command swap, the
pause target plan and the touch-button suppression stay off; both native
targets are kept. The title, the normal / Master Quest choice (game mode 1)
and name entry keep the 3.1 presentation; Reframed's name entry is the same
full-screen keyboard.

## Sites

Reframed patches call sites inside blocks. The whole-AOT title module only
reaches block starts, so each site is moved to the block before the call
(emulating its argument instructions) or to the call's return. The title
module needs no new sites.

| Site | Reframed site | Behavior |
| --- | --- | --- |
| `0x00300400` | `0x00300404` | Scene at viewport (119,-33,300,250): Link to the right. |
| `0x0030046C` | `0x00300468` | Full viewport after the scene. |
| `0x003004C8` | `0x003004D0` | Caption board at (56,2,264,192), composited 4x while the caption shows. |
| `0x003004D4` | `0x003004DC` | Presentation at (290,114,324,324). |
| `0x00300508` | `0x00300518` | Caption glyphs at (56,2,264,192); records the caption context. |
| `0x00300520` | `0x00300520` | Logo at (290 or 392,114,324,324), logo and fade split. |
| `0x0030055C` | `0x0030055C` | Lower file interface replayed at (56,164,336,224). |
| `0x0041EE38` | `0x0041EE50` | Leather base board under the slots not drawn. |
| `0x00433810` | `0x0043380C` | Board geometry after the native regeneration (below). |

The interface replay draws `PauseUi_Draw` (`0x0041EC50`), the overlay groups
(`0x0041F308`) and the lower callback. Reframed excludes Back's corner with an
exclude scissor and then draws Back alone at the bottom-left edge. The host
Vulkan backend has no exclude scissor, so two disjoint include scissors cover
the rest of the viewport. TopScreen has no Back there, and its pass is not
replayed.

Reframed draws the scene at (119,-8,300,200). On the rotated surface that is
300/480 of the height but 200/400 of the width, so Link is 80% as wide as the
scene's projection intends. TopScreen keeps 300/480 on both axes (width 250
around Reframed's horizontal center 92), so Link keeps his proportions at any
output aspect ratio.

## Board geometry

After each native regeneration of the 184-quad file board, as Reframed's
`UpdateFileBoard`:

- the heading board is 60% wide and 70% tall (100% for copy and erase
  prompts, 112% for their confirmations) and hidden from the first exit tick
  (states 4, 5) through the Start/Copy/Erase page (7..10);
- Start is 60%, Copy and Erase are 90% of the resized Start, with their pressed
  frames and the cursor;
- the copy/erase controls (states 11..42, quads 134..161) are 60% around each
  control's center;
- names, badges, timestamps and hearts are 80%; a name keeps its action-page
  row through its confirmation (states 35..42);
- the heading caps end at atlas column 286.5, so no transparent texel filters
  in.

Reframed shrinks the cursor in every state from 11 to 42, which also shrinks
it inside the copy target's file slot. Here the cursor shrinks only while its
frame is centered on one of the scaled controls.

## Verification

`oot3d_top_screen_reframed_file_select_tests` covers the gate, every site's
call sequence, the scissors, the logo split, the leather skip and the board
geometry, including the confirmation controls and the cursor rule.

Developer launch with the catalogued product title module, captured
deterministically: file list, Start/Copy/Erase page, copy to an empty slot
through "File copied.", the erase confirmation and its Cancel, name entry on
an empty slot and the version choice. The Azahar oracle with only Reframed's
EUR mod is the reference for the left half; Azahar does not draw Link (Reframed
issue #17), so issue #16's image is the reference for the right half.
