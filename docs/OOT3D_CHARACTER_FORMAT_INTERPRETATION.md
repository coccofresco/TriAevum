# OOT3D Character Format Interpretation

This is the shared interpretation contract for OOT3D character CMB conversion.
Child Link is the first validation target, but the rules below are general.

## Evidence

The confirmed-good inspection asset is:

`I:\oot3dre_work\character_static_glb\link_child_static_base_character.glb`

It establishes these facts for the current skinned character path:

- CMB primitive identity must be preserved as `(mesh_index, primitive_index)`.
- The clean visible subset is a draw selection, not a parser special case.
- Skinned static vertices must use `source_position` and `source_normal`.
- Rigid face primitives must use source shape vertices transformed by their
  referenced bone.
- Material texture assignment follows the primary material texture slot.
- Embedded GLB/native texture export uses `flip_y`.
- UVs remain `normal`.
- `bind_pose_preview_position` is diagnostic only; it is not runtime geometry.

## Shared Selection

GLB inspection and native runtime export both consume the same manifest format:

`oot3d_character_mesh_selection_v1`

The manifest selects exact CMB primitive keys and may also carry texture/UV
orientation metadata. It is validated against the parsed CMB model before any
export writes resources. A wrong model name, missing mesh, missing shape, or
missing primitive is a hard failure.

Current Child Link profile:

`tools\oot3d\oot3d_asset_tool\profiles\link_child_static_base_selection.json`

This profile is data. It is not a code branch, and equivalent profiles should
be added for other characters or equipment states only after their primitive
identity has been verified.

## Child Link Base Evidence

The Child Link base profile is not justified only by visual confirmation. The
current structural evidence from `childlink_v2` is captured by:

`I:\oot3dre_work\character_visibility_reconstruction\link_child_visibility_reconstruction.json`

The audit was generated with:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool audit-character-visibility-reconstruction `
  E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor\zelda_link_child_new.zar `
  --cmb-name child/model/childlink_v2.cmb `
  --selection-manifest tools\oot3d\oot3d_asset_tool\profiles\link_child_static_base_selection.json `
  --n64-player-lib E:\ppssppvr\oot3d_decomp\external\oot\src\code\z_player_lib.c `
  --output I:\oot3dre_work\character_visibility_reconstruction\link_child_visibility_reconstruction.json
```

Key findings:

- The selected subset is coherent by CMB primitive identity: 12 exact primitive
  keys across 10 meshes, out of 57 primitives in `childlink_v2`.
- The selected visibility ids are `000`, `003`, `024`, `025`, and `026`.
- Visibility `000` and `003` are the symmetric open-hand draw groups. They are
  single skinned mode-2 primitives using `childlink_02` on the terminal hand
  bone palettes.
- Visibility `001`, `004`, and `007` are similar hand/fist alternates that also
  use `childlink_02`, but they are separate draw states, not extra body parts.
- Visibility groups `002`, `005`, `006`, and `016` through `020` mix hand
  surfaces with `p_tex*` equipment/prop surfaces. Exporting every skinned
  primitive would include those inactive equipment states.
- The selected body group is visibility `024`: meshes `044` and `045`, four
  skinned mode-2 primitives, 857 triangles, textures `childlink_01` and
  `childlink_00`.
- Mesh `046` is a skinned mode-2 body continuation using `childlink_01`, with a
  body/head-like bone palette; it is structurally compatible with the selected
  base body group.
- Visibility `025` proves that visibility id alone is too coarse: mesh `046` is
  selected, but mesh `047` is not. Mesh `047` is only 20 triangles, mode 1, and
  uses the leg-palette pair `[4, 7]`; it is not part of the clean base body.
- Visibility `026` is a coherent head/face group: mouth, head, two face
  surfaces, and eyes. It contains the two rigid expression primitives
  `c_mouth01` and `c_eye01` plus skinned face/head primitives.
- The selected hand meshes `000` and `005` are single skinned mode-2 primitives
  using `childlink_02`, paired on the expected hand/arm bone palettes.
- The N64 reference in `z_player_lib.c` shows the behavior model this mirrors:
  `gPlayerModelTypes` selects model slots for left hand, right hand, sheath,
  and waist; `Player_OverrideLimbDrawGameplayDefault` swaps the active hand,
  sheath, and waist display lists; `Player_DrawImpl` maps animated eye and
  mouth indices to texture segments. OOT3D keeps the same draw-state concept but
  stores those choices as CMB mesh/visibility groups.

What is not fully reconstructed yet:

- A general automatic classifier for "base character" across all characters.
- The original OOT3D draw-state table is not yet named symbolically. The current
  reconstruction is still structural: CMB visibility groups, primitive keys,
  texture families, bone palettes, and N64 Player draw-slot behavior.

For that reason, the current workflow treats the Child Link base subset as a
validated selection profile, not as an inferred universal rule. Generality comes
from the shared parser, texture policy, geometry sources, selection manifest
schema, and validation gates.

## Export Backends

Both GLB and native runtime export follow the same front half:

```text
container path -> CMB payload -> parsed CMB model
             -> shared texture profile
             -> optional character mesh selection
             -> backend-specific writer
```

The GLB backend writes embedded textures and named glTF nodes for inspection.
The legacy Fast-resource adapter writes Texture, Vertex, and DisplayList
resources for compatibility tests.
The backends differ only in representation; they must agree on primitive
identity, texture selection, position source, normal source, and UV contract.

## Draw Profiles

- `all_meshes`: export every primitive the backend supports.
- `selected_primitives`: export only keys from a validated selection manifest.
- `base_character`: CLI alias for `selected_primitives`.
- `skinned_primitives_only`: export every primitive with skinning mode 1 or 2.
- `rigid_primitives_only`: export every primitive with skinning mode 0.
- `visibility:<id>`: export meshes with that CMB visibility id.

`skinned_primitives_only` is a technical diagnostic profile. It must not be
used as a substitute for a clean character subset.

## Validation Gates

For a selected character subset, the minimum acceptance gate is:

- GLB manifest and native manifest list the same `selection_primitive_keys`.
- GLB and native manifests report the same selected mesh and primitive counts.
- Native audit passes against the bind-pose JSON using the manifest selection
  filter.
- Native audit reports `position_source = source_position`.
- Native audit reports zero skinned primitive bounds mismatches.
- No exporter path contains model-name or texture-name conditions for the
  selected character.

Current Child Link selected-base validation:

```text
selected mesh count: 10
selected primitive count: 12
selected skinned primitive count: 10
selected rigid primitive count: 2
native skinned primitive bounds mismatches: 0
native audit issues: 0
```

## Runtime Policy

The fork should consume normalized native resources and compact runtime
profiles. It should not need raw OOT3D CMB interpretation at runtime for the
static bind-pose path. Raw format behavior is reconstructed offline, validated,
and then emitted as the fork's native resource graph.

If a later character requires alternate equipment, expressions, or visibility
states, the first correction should be a new validated selection/runtime profile
that uses the same parser and backend contracts. Do not add per-character code
branches unless a new format rule is proven across source data.

## Animation GLB Diagnostic Export

Animation interpretation is first validated with a diagnostic baked-morph GLB.
This keeps the confirmed static mesh/material/UV path unchanged and varies only
vertex positions/normals by sampling CSAB bone poses.

For Child Link running with free hands, the current reference command is:

```powershell
$env:PYTHONPATH='I:\oot3dre\tools\oot3d\oot3d_asset_tool\src'
python -m oot3d_asset_tool export-skinned-animation-glb `
  E:\ppssppvr\oot3d_decomp\work\extract\romfs\actor\zelda_link_child_new.zar `
  --cmb-name child/model/childlink_v2.cmb `
  --csab-name child/anim/nml_run_free.csab `
  --selection-manifest I:\oot3dre\tools\oot3d\oot3d_asset_tool\profiles\link_child_static_base_selection.json `
  --output I:\oot3dre_work\character_animation_glb\link_child_nml_run_free_step_base_hands.glb `
  --manifest-output I:\oot3dre_work\character_animation_glb\link_child_nml_run_free_step_base_hands.manifest.json `
  --frame-step 1 `
  --fps 60 `
  --interpolation STEP
```

The export contract is:

- frame `0` is emitted as the base GLB mesh;
- later integer CSAB frames are emitted as position/normal morph deltas;
- one animation channel per primitive drives identical one-hot morph weights;
- the diagnostic default uses `STEP` interpolation so Blender/viewers do not
  synthesize non-CSAB in-between deformations;
- selected rigid primitives follow their single animated bone;
- selected skinned primitives use the CMB source vertices plus decoded weights
  through `pose_world * inverse(bind_world)` skin matrices;
- CSAB rotation channels are sampled with angular unwrap across the `-pi/+pi`
  boundary before Hermite interpolation;
- texture assignment, texture orientation, UV orientation, and scale come from
  the same character selection profile as static base GLB export.

This GLB is a diagnostic oracle for animation interpretation. The runtime path
may later use native skeleton/animation resources instead of morph targets, but
it must match this sampled deformation before being considered equivalent.
