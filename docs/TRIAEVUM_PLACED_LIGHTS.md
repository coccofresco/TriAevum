# Placed scene lights (F1 > Lighting > Lights & Shadows > Scene lights)

2026-10-07. Lights a user adds to a scene from F1, beside the scene
environment's key and fill lights. They belong to the scene's environment
profile, so a scene with a night profile keeps separate lights by night
(creating the night profile copies them).

## Editing

**Add light at Link** creates a light where Link stands, 50 units above his
feet, facing his direction and tilted 30 degrees down; **Move to Link** and
**Face Link's direction** bring an existing light back. The position (world
units, Y up) and the rotation can then be set freely. At most 16 lights per
profile. The controls follow the type:

| Type | Position | Rotation | Shape and size | Shadows |
|---|---|---|---|---|
| Sun | no | yaw, pitch (the light's direction) | none | never (the key light keeps the directional shadows) |
| Spot | yes | yaw, pitch (the beam) | reach, beam angle, beam softness | yes |
| Point | yes | no | reach | yes |
| Surface | yes | yaw, pitch, roll (the emitting face) | rectangle (width, height) or disc (radius), one or both sides, spread, reach | yes |
| Omni | yes | no | reach | yes |

Every light has a name, visibility, colour (linear RGB) and intensity (0 to 16).
Yaw 0 faces +Z and 90 faces +X (the game's own yaw); pitch 90 points up. Edits
preview live; **Save scene environment** keeps them, **Reload** discards them.
They apply while **Use scene environment** applies (Modern lighting, F2 off).

## Rendering

Placed lights have their own per-draw slots (`PicaSurfaceLightingUniforms::Placed`,
16, count in `Control[3]` bits 8-15), lit in the per-vertex/Modern path and in
the PBR path (GGX), and they seed global illumination like the other dynamic
lights. Position is in view space, axis and tangent in world space; distances
are world units. The terms (`pica_surface_placed_light`):

| Type | Term |
|---|---|
| Omni | `1 - d^2/r^2` to the reach `r`, as Navi and the fairies |
| Point | `(100 / max(d, 10))^2`: intensity 1 is the colour at 100 units; times a smooth cut `(1 - (d/r)^4)^2` at the reach |
| Spot | the point term times `smoothstep(cos outer, cos inner, cos angle)`; outer is half the beam angle, inner shrinks it by the softness |
| Surface | rectangle or disc: direction to the shape's closest point, form factor `A cos / (pi d^2 + A)` (the colour times intensity in contact with it), cut at the reach; one-sided surfaces emit from the front face only. A spread below 180 degrees (see below) limits `A` to the footprint and raises it by a gain |
| Sun | parallel light along its direction, no falloff, never shadowed |

**Spread** (surfaces, 1 to 180 degrees, 180 by default) is the full angle
around the axis within which each point of the surface emits, as an area
light's spread in Blender. At 180 the surface is diffuse. Below, its light is
straightened: a fragment at height `h` above the surface's plane is reached
only from the part of the shape inside a footprint of radius
`h tan(spread / 2)` around its projection (for a rectangle, the square of the
same area; for a disc, the exact overlap), and the term uses that part's area
and its centroid's angle. A gain, the diffuse surface's form factor straight
under its centre over the footprint's, keeps the light under a surface that
covers the footprint as bright as at 180; beside the surface it falls off
within `h tan(spread / 2)` of the shape's edge. The renderer takes whole
degrees: `PicaPlacedLightUniforms::AxisKind.w` adds 16 times `180 - spread`
to the kind's code, so a diffuse surface's uniforms do not change. The field
`spread` is optional in the profile (180 when missing).

**Cast shadows** gives spot, point, surface and omni a point-shadow cube map at
the light's position (Point shadows: Cube maps; 8 rows shared with Navi and the
fairies). That is exact for spot, point and omni, approximate for a large
surface (its centre casts).

Changing the lighting shader changes every lit pipeline: the built-in fragment
families (`pica_*_fragment_binaries.h`) are regenerated with it
(`build_fragment_artifacts`, checked by `tev_generator_consumer_tests`). Forge's
portable shader corpus (`expanded-union.o3ps`) is keyed by generated source as
well: it was regenerated with this lighting on 2026-10-07 by
`regenerate_shader_corpus.py` (see the
[scenario preparation](TRIAEVUM_SCENARIO_SHADER_PREPARATION.md#regeneration-with-the-current-runtime-2026-10-07)
document) and is regenerated again with each release runtime.

## Gizmos

"Show light gizmos" (on by default) draws, while the placed-light editor is
open and while the developer free camera is on, every light's shape in its
colour: omni and point their reach as three great circles, spot its cone (and
its hard core), surface its rectangle or disc with arrows to its reach from
each emitting face, sun a sheaf of arrows along its light; the selected light
bright, the others fainter, hidden lights faintest. Through the selected light
run its move axes (world X red, Y green, Z blue) and turn axes (yaw about Y,
pitch about its horizontal right axis in orange, a surface's roll about its
own axis in cyan; a spot's or sun's beam in pale yellow), 20000 units each
way, with rings of a constant size on screen.

The lines are real 3D lines (`Fast::Oot3d::WorldDebugLineRuntime`,
`WorldDebugLinePass`): screen-space quads of a constant width drawn in the
scene's render pass before its first UI draw, tested against the scene's
depth without writing it. Parts in front of the scene's surfaces are solid;
parts behind them are dashed and faint, so an axis shows where it enters and
leaves a surface. Their pipelines are built (and their two shaders compiled)
the first time there are lines, never at startup.

## Storage

`placedLights` in the profile JSON (`environments/scene-XXXX[.night].environment.json`),
optional within version 5, so runtimes that predate it ignore the lights:

```json
"placedLights": [
  {"id": 1, "name": "Light 1", "type": "omni", "visible": true,
   "color": [1.0, 0.8, 0.6], "intensity": 1.5,
   "position": [-504.0, 430.0, -1224.0], "rotation": [90.0, -30.0, 0.0],
   "castShadows": true, "radius": 300.0, "width": 100.0, "height": 100.0,
   "shape": "rectangle", "twoSided": false, "spread": 180.0, "beamAngle": 45.0,
   "beamSoftness": 0.2, "range": 300.0}
]
```

`type` is `sun`, `spot`, `point`, `surface` or `omni`; `shape` is `rectangle`
or `disc`. `id` is unique in the profile and names the light's shadow row.
Every field but `spread` is required in an entry and range-checked; an
invalid entry makes the profile invalid, as any other out-of-range value does.

## Verification

- `scene_point_light_gpu_tests` runs the production lighting GLSL on the GPU:
  15 placed-light cases (omni, point at 100 and 50 units and beyond its reach,
  spot on and off its axis, rectangle facing, one-sided and two-sided facing
  away, disc, sun; a 30-degree rectangle and disc over the fragment as bright
  as the diffuse ones, the rectangle beside its footprint dark, a diffuse
  rectangle beside the fragment still lit) beside the 24 scene and 10 shadow
  cases.
- `build_fragment_artifacts --check` (all five families) and
  `tev_generator_consumer_tests` after regenerating the packs; only the uber
  family contains the lighting term and changed.
- `triaevum_f1_settings_smoke`: a light added at Link's placement lands 50
  units above him with his yaw (axis checked) and is drawn as a point; spot
  publishes its cone cosines; surface its rectangle and disc; sun offers no
  shadows; omni with shadows publishes a shadowing light; save writes
  `placedLights`; duplicate gives a new identity; delete stops drawing; reload
  restores the saved light. Gizmos: the editor publishes a light's reach and
  move axes, none once "Show light gizmos" is off; lines are cut at the near
  plane and turned into the physical framebuffer.
- Live, Kokiri Forest from the play quick save, against the same frame without
  lights: an orange sun at pitch -45 raises the mean RGB from (56, 59, 21) to
  (83, 73, 22); a green 200x200 rectangle 200 units above Link greens the
  platform (mean G 59 -> 67); a blue 90-degree spot 150 units above him draws
  its cone's edge on the tree around the door (around Link, mean RGB
  (76, 77, 24) -> (86, 94, 36)).
- Gizmos, live (developer build, free camera fixed above Kokiri Forest's
  stump, a spot, a green omni and a blue two-sided panel): every shape lands
  where its light lights the scene (the green omni's sphere around the
  green-lit ground right of the stump), the spot's cone is dashed behind the
  stump and the omni's sphere below the ground, the HUD stays over the lines.
