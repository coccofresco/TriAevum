# SingleScreen 3.0 assets

TriAevum ships two files of **SingleScreen 3.0** (the successor of TopScreen /
Single Screen Experience for The Legend of Zelda: Ocarina of Time 3D, by
**M-1** / rlgcarrot, <https://gamebanana.com/mods/695893>), taken unchanged
from the official archive `singlescreen_30_25aeb.zip` (SHA-256
`5668640fec8dc2b47b4516fd0b871c4d7d90ca1c2203acc2a22adb59aaab5993`):

| Package path | Archive member | SHA-256 |
|---|---|---|
| `resources/topscreen/singlescreen-3.0/custom_ui01.ctxb` | `romfs/menu/custom_ui01.ctxb` (low-resolution UI atlas) | `20290e417c56973cd19daa1f61a6c3963f075d61776db3d996954ee3296d07d8` |
| `resources/topscreen/singlescreen-3.0/zelda_link_ext.zar` | `romfs/actor/zelda_link_ext.zar` (Link animations) | `06d60e05c493e86677497eb935b49951d613249c43f896816ae898bb8b005902` |

SingleScreen 3.1 (`singlescreen_31_989c3.zip`, SHA-256
`f91485999a6b8e89b8d574064dc03c1c20704f2c718e93c5187251b6ef69dbcb`) carries
the same two files byte for byte. TriAevum also ships, unchanged, its HD Latin
font, identical in the archive's EUR and USA layouts:

| Package path | Archive member | SHA-256 |
|---|---|---|
| `resources/topscreen/singlescreen-3.1/ltn16.qbf` | `SingleScreen_3.1/EUR/Emulator/load/mods/0004000000033600/romfs/message/eu/ltn16.qbf` (HD Latin font) | `8b83e081f721c6e1842b0a9cd90b71a3fb2849674d2b1040f3dc5c3221594169` |

They are distributed with their author's permission, as the maintainer reports
it on 2026-10-16 for the 3.0 files and on 2026-10-21 for the HD font
(`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`). All rights in these files remain
the author's; TriAevum's GPL
does not apply to them, and any use outside TriAevum needs the author's own
permission.

Not distributed: the archive itself, its IPS patch and ARM payload (TriAevum
reimplements the behavior as typed host code), its `sys8.qbf`, and the HD UI
atlas (by chubbycubby). Forge takes the HD UI atlas from chubbycubby's HD UI
texture pack when the user obtains it (`LICENSES/OoT3D-Vanilla-UI-HUD.md`).

The original game, its characters and its assets belong to Nintendo; these
files are not a license to any of them.
