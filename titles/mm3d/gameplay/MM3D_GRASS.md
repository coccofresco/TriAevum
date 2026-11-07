# MM3D grass adapter

The source/mask editor, saved grass preset, placement caches, blades/cards,
wind, interaction field and coarse/refined grass shadow grids are shared.
MM3D supplies only its rule-file namespace and native collision facts.
No authored grass rule is installed by default. The owner's example and
captures live outside the repository in `W/analysis/parity/GRASS`.

## Rules

`TitleSession::GrassRuleNamespace()` selects `mm3d`. The host stores only
source and mask rules in `<settings directory>/grass/mm3d/rules.json`, with
independent `Grass` and `GrassSavedPreset` arrays. F1 shows that path. The
legacy configuration's rules are retained for OoT3D, whose storage is unchanged.
PM0's exact foreign-default migration remains in force. No per-scene rules
or scene lists exist in code; an authored rule selects the native base-level
texture content identity and dimensions, as on OoT3D.

## Collision facts (EUR v1.1)

Reader: `mm3d_actor_interactions.h`. Semantics were consulted read-only in:

- zeldaret-mm `include/z64actor.h`, `z64collision_check.h`, `z64player.h`;
  `src/overlays/actors/ovl_player_actor/z_player.c` (body cylinder updated
  from the current form and pose), `ovl_En_Horse/z_en_horse.c`;
- 2ship2harkinian's corresponding MM files, including Player_Update's native
  body-cylinder and OC registration, not its enhancement-size policies.

These are semantic references, not address/layout donors. The EUR code image
SHA-256 begins `6eec27b1`; the reader checks 19 guard words at image mount.
Wrong/unreadable image => empty interaction snapshot, never guessed dimensions.

- Existing guarded live-Play reader: current View `*[0x656B5C]`, play = View
  minus `0x1F8`, state type 4 and camera backpointer required.
- Twelve category lists start at play `+0x20C0`, stride `0xC`, count `+0`,
  head `+4`. `Actor_RemoveFromCategory` at `0x1724AC..0x1724E8` reads the
  category byte, computes `category * 3 * 4`, decrements the context's
  count `+0x10`, and installs `Actor.next` (`+0x134`) as head `+0x14`.
  The established actor-context base is play `+0x20B0`.
- Actor world position `+0x24`, update `+0x140`, draw `+0x144`, next `+0x134`.
  Non-drawable/dead actors, unreadable records, category mismatches and invalid
  dimensions are excluded; lists are count-bounded and globally cycle checked.
- Generic collision-check cylinder: signed halfwords `+0xB4/B6/B8` for
  radius/height/Y shift. `CollisionCheck_SetInfo2` at `0x4D6454` copies
  initialization fields to `info+0x10/12/14` (Actor info starts `+0xA4`).
- Player (id 0, category 2): **do not use generic 10×10 colChkInfo**. Native
  body cylinder at `+0x9BC`, initialized by `0x204318..0x204324` from
  `player+0x800+0x1BC` with owner=player. Owner and shape 1 are checked.
  Dimensions are **float** radius/height/Y shift at cylinder `+0x40/44/48`.
  Player update writes height/shift at `0x207254/0x207328`; its cylinder
  position updater `0x2183FC` copies world position to `+0x4C/50/54`.
  Form byte `+0x1FF` (read by `0x1F5FC8`) accepts all five native forms.
  Dimensions follow the game's actual current form/pose, not a renderer table.
- En_Horse id `0x000D`, native type byte `+0x204` equal to 2 (MM's child
  Epona), publishes Epona's role for the existing local shadow refinement.
  EUR init `0x39B508/0x39B50C` stores type 2 for `ENHORSE_PARAM_4000`;
  update `0x2B5D04/0x2B5D14` reads/compares it. Bandit/donkey and unreadable
  types remain SceneActor, as do other eligible actors. No actor IDs reach
  shared rendering code.

World coordinates and native units are retained. Identity is actor id/address,
context is live PlayState, and frame is the completed visual source sequence,
matching OoT3D's publication timing. The existing bridge handles interpolation,
teleport/despawn and reset history. This also supplies the existing water-ripple
consumer without changing guest collision or gameplay.

## Evidence and limits

`W/analysis/parity/GRASS/collider_proof.json` checks every guard against EUR
code.bin, and decodes Clock Town, Termina Field and Stock Pot Inn offline.
All three are Human form: live float body radius 12, height ~37–38, Y shift
~4.81–4.93; generic info remains 10×10. Body position equals Actor.world.
Termina's En_Horse is native type 2, radius 35, height 100. Other forms are covered by synthetic
reader tests with changing native dimensions, not by five gameplay runs.

The shared frontend also recognizes the native indexed affine UV subroutine
by instruction/flow semantics. Optional uniform guards admit only its model
entry, direct UV modes and zero matrix-row offset; generated/projected/other
entries fail closed. The existing affine sampler/placement/mask code is reused.
No title/program hash or GLSL string match enables it. Legacy affine layouts
retain their original memo-key bytes; new guards and their uniforms participate
only for the new contract.

Generated grass on fragment-lit terrain uses the shared native-fragment atlas
variant (`pica_fragment_grass_lighting.h`): canonical fragment lighting and its
LUTs, a register-proven multiplicative TEV response, native vertex tint/AO and
existing shadow recomposition. Its illumination factor keeps HDR gain until
blades apply their albedo: a unit-white TEV evaluation would prematurely clip
`light * tint * 4`. The capability accepts texture0 × fragment-primary plus
optional vertex tint, with no scaling/clamp before tint. It is per-vertex grass
lighting, not a claim of texel-exact framebuffer/TEV quantization reproduction.
The old OoT3D vertex atlas shader bytes are unchanged. Other TEV expressions,
bump-map and native texture-shadow classes retain the previous fallback.
