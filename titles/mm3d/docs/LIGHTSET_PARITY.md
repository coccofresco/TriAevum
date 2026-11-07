# MM3D added-light controls

LIGHTSET, based on integrated `b1e79383`. Shared ownership is documented in
`docs/TRIAEVUM_SCENE_POINT_LIGHT_SETTINGS.md`.

## Title facts and existing producers

- `mm3d_native_a32_lighting.cpp`: guarded EUR En_Elf `0x10`, live normalized
  params 0 -> companion group 0 (Tatl); other params -> fairy group 1.
  Every other positively proved native actor source -> group 2.
- `mm3d_synthetic_emitters.h`: dormant visible En_Elf uses those same groups;
  synthetic fires/Keese/arrows/walls/spouts/projectiles -> group 2.
- Shared `CapturePicaRoomEmitters` uses group 3 for the title's semantic room
  catalog, including native/fake emissive surface-following lights. Artist PBR
  emissive maps remain a separate surface channel, as in OoT3D; they are not
  automatically point lights.
- `mm3d_title_session.cpp` supplies **Tatl** as the display name of group 0.
  No MM fact is added to the shared renderer or the OoT actor adapter.

No new guest addresses, callback assumptions or actor classifications were
introduced. Existing exact EUR guard/node/callback proofs remain mandatory.
Semantic cross-check: read-only zeldaret-mm and 2ship2harkinian `z_en_elf.c`
normalize actor params to fairyType; case 0 installs the companion update and
Tatl hint/timer behavior. Native/light tests cover params 0/1/5 for every
registered source, plus the existing per-guard corruption/visibility tests.
This does not claim unimplemented actor classes from the wider G05 inventory.

## Persistence and defaults

The four rows match OoT's granularity: Tatl, Fairy, Flame, Architecture. They
are not collapsed into two switches and not duplicated in Graphics settings.
User profiles: `<config>/environments/mm3d/scene-XXXX[.night].environment.json`
and `group-*.environment.json`. Release defaults: the same six group filenames
under `runtime/triaevum_resources/environments/mm3d/`.

All point defaults already match OoT (enabled, unit intensity/radius, white
RGB); the real release-loader test compares every day/night group endpoint.
**GP profile values and files are unchanged.** Runtime tests cover separate
scene/day/night/group saves, reload, disabled-profile point publication, and
no writes into the OoT namespace. Actual-widget tests exercise all four
controls and graphics-preset changes without stealing their settings.

## Corrected native-fragment Off semantics

Before LIGHTSET, disabling a group or setting intensity zero silently restored
its native direct/specular term. OoT removes that term. LIGHTSET uses the
existing zero-gain native-fragment recomposition after the same identity proof,
retains ambient and emits no row/shadow for that group. Other groups are
untouched. This deliberately changes MM Modern **Off** captures from older DL
receipts; their “Off = native” labels remain historical, not new references.
Native/Authentic admission and OoT legacy shader families are unchanged.

Local qualification receipts and run counts are outside Git under
`W/analysis/parity/LIGHTSET/{PROGRESS,RESULTS}.md`. This document describes
implementation, not an owner look approval or full-title parity verdict.
