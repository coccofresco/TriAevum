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
the same two files byte for byte.

They are distributed with the author's permission, as the maintainer reports
it on 2026-10-16 (`docs/TRIAEVUM_RELEASE_APPROVAL_RECORD.md`). All rights in
them remain the author's; TriAevum's GPL does not apply to them, and any use
outside TriAevum needs the author's own permission.

Not distributed: the archive itself, its IPS patch and ARM payload (TriAevum
reimplements the behavior as typed host code), and its high-resolution
graphics: the HD UI atlas and the HD fonts. These stay out of the package until
their author consents. Forge imports them only from the archive when the user
places it beside Forge.

The original game, its characters and its assets belong to Nintendo; these
files are not a license to any of them.
