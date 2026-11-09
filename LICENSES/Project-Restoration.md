# Project Restoration (Moonfall Curse)

- Upstream: <https://github.com/leoetlino/project-restoration>, commit `8bb2a7b8`
  (release `mm3d_project_restoration_plus_hd_hud_v1.5.9-5-g8bb2a7b`), by leoetlino and contributors.
  The HD HUD layouts come from the `mm3d_big_screen_layout_779512b` archive linked by the official
  Project Restoration HD HUD page.
- License: GPL-2.0-or-later (see `GPL-2.0-or-later.txt`).
- Upstream notice: the GPL notice covers Project Restoration's source; it is **not** a claim that original Nintendo
  material inside the layout archives is GPL-licensed. No upstream endorsement is implied.

## What Moonfall Curse uses

- Guest-side logic ported to C++ under `titles/mm3d/restoration` (see its `PORT_NOTES.md` and `COPYING`).
- HD HUD layout files `layout/Joker.Main.New.gar`, `layout/Joker.Main.Play.gar` and `layout/Joker.mfpj`. **The
  package ships none of these files.** The Forge rebuilds them at install time from the user's own EUR game dump with
  the recipe `resources/mm3d/project-restoration/layout.recipe.json` (format `triaevum_restoration_layout_v1`):
  - members identical to the game's `layout/Joker.Main.Play.gar` are read from the user's dump;
  - members Project Restoration modified, and `Joker.mfpj`, are rebuilt with the BPS patches in
    `resources/mm3d/project-restoration/layout/patches/` (TriAevum data, GPL-2.0-or-later);
  - the members with no counterpart in the game ship as Project Restoration distributes them, under its license, in
    `resources/mm3d/project-restoration/layout/members/`:
    `New.mfpk`, `ItemIconBase.mfl`, `HudDpad.mfl`, `AActionIconBase.mfl`, `Hud2.mfl`, `HudItemIconSet.mfl` and
    `hud_patrs01_action_text.ctxb`.

The recipe and patches are produced offline by `tools/mm3d/restoration_layout.py`; its `check` mode proves the rebuilt
files are byte-identical to the upstream release files.
