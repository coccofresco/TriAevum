# dekui presentation defaults

Unchanged `resources/mm3d/defaults/presentation.json` from tgacquin/mm3d-dekui
4f2692ed (= release v1.1.0 plus a README commit), GPL-2.0-or-later. These are
author-created settings, not game text or assets. The `.inc` embeds identical bytes
for startup fallback (regenerated with `W/tools/dekui/embed_presentation.py`, which
reproduces the previous 6e36dfb4 embed byte for byte).

608 top-level keys: 20 added and 17 values changed relative to 6e36dfb4 (fresh-install
HUD layout version 2 with mirrored item buttons, minimap opacity 0.75, stick-click
controls on for a first launch, dungeon-map arrows and prompts). 10 more keys are
parser-only: they are never shipped, always saved and roundtrip exactly.
SHA-256: `bd0aeb13f0995e858154e9edcabc9304b8bacba53120e492e7e39a839b152714`.

A profile without `player_hud_layout_version` (any profile saved before 4f2692ed) stays
layout version 0 and keeps its own HUD sizes; only these shipped defaults are version 2.

All preferences roundtrip through the updated schema. Behavior still pending
in this partial port is listed in [UPDATE_PORT_NOTES.md](../UPDATE_PORT_NOTES.md).
