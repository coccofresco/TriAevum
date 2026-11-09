# mm3d-dekui (Moonfall Curse)

- Upstream: [tgacquin/mm3d-dekui](https://github.com/tgacquin/mm3d-dekui), the single-screen Majora's Mask 3D
  mod by tgacquin and contributors, release v1.0.0-beta.1
  (source revision `6e36dfb4` for the update port). Its source is distributed with a GNU General Public
  License, version 2 (`license.txt` in the upstream source).
- License: GPL-2.0, as distributed upstream (the GPL version 2 text is in `GPL-2.0-or-later.txt`).
- No upstream endorsement is implied.

## What Moonfall Curse uses

- The guest-side behaviour of mm3d-dekui (its interface on one screen, its options and its fixes) is
  ported to C++ under `titles/mm3d/dekui` and `titles/mm3d/adapter` (see `titles/mm3d/dekui/PORT_NOTES.md`
  and `UPDATE_PORT_NOTES.md` in the corresponding source). It is part of Moonfall Curse's own code: mm3d-dekui
  is not a Forge input.
- No mm3d-dekui binary and no game-derived file from its release is shipped. Files that come from the game
  are prepared by the Forge from the user's own dump at install time.

## Notice

The GPL covers mm3d-dekui's source and our port of it. It is not a claim about any Nintendo material:
none ships in this package.
