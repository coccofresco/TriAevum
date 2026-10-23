# Baked outdoor surface lighting

PerPixelPBR uses the shared per-pixel surface evaluator for ordinary textures.
An installed PBR material selects the GGX evaluator. Both now treat native
baked room colour as the **lit reference**, rather than adding the sun to an
already lit image.

## Ownership

`BuildPicaSurfaceLightingUniforms` admits a surface when native vertex lighting
is disabled or no enabled native light supplies diffuse RGB to a nonzero
material diffuse channel, and its captured scene has a sky light. A nonzero
material coefficient with zero light diffuse is still ambient-only. This
classification reads native inputs before profile gains or source deduplication,
so suppressing a directly lit actor's native lights cannot turn it into a baked
surface. Sky lights are the adapter's clock-driven sun/moon
records. No scene-ID list, image statistics, exposure or fitted multiplier is
used. The reference normal is world up, transformed by the captured view's
inverse transpose.

Native presentation never enters this path. The backend also clears admission
for NativePBR, isolated lighting and native fragment lighting. Directly lit
actors and static-light interiors keep their previous equations. This is not
a change to the interior-sun shadow cache or its room classification.

## Equation and controls

For ordinary textures, let `C` be the native, gamma-encoded primary colour,
`A` the retained ambient response, `D_i` the directional colour/material
response, `g_i` its gain and `v_i` its shadow visibility. Then:

```
reference = A + sum(D_reference_i * max(up . L_i, 0))
response  = A + sum(D_i * g_i * min(max(N . L_i, 0), max(up . L_i, 0)) * v_i)
primary   = C * response / reference
```

The key's reference always uses intensity **1** and its untinted scene colour;
the numerator uses the saved intensity and tint. Other directional sources
use their current weights. A zero denominator contributes zero. The bounded
orientation term prevents division by a low sun elevation from overexposing
facing walls. Local/placed lights and independent rim remain additive.

For unlit geometry, `A` contains the existing native ambient gain (1 with the
profile off or preservation on), plus authored ambient. Ambient-only geometry
uses its material-weighted resolved ambient instead. With a profile enabled,
ambient and fill now set the distribution between lit and shadowed areas on
these surfaces. They do not add an unrestricted gain to the horizontal lit
reference. Key intensity still controls brightness: with no ambient, 0.5 gives
half the encoded native colour, 0 gives black, and values above 1 can brighten.
The white 1x key preserves the native day/night colour on an unshadowed flat
surface. Key tint remains an intentional colour change.

PBR evaluates the same GGX material twice: the actual normal/visibility and
the unshadowed horizontal reference. Its directional response is bounded by
that reference before applying intensity, tint and visibility. The resulting
dimensionless ratio modulates the encoded native TEV result, then is decoded
once for linear local lighting and emission. This keeps the existing Fresnel,
roughness, metallic and native Lambert/pi convention without an exposure
constant. Material AO affects the numerator only; its guide and GI source
weights follow the calibrated contribution. Optional effects are not enabled
by calibration.

## Why the previous path gained brightness

The ordinary surface path added scene sunlight to the full native unlit
primary colour. Its profile path could replace that primary with a newly
computed ambient-plus-key term, losing the native brightness reference.
The PBR path likewise either added sunlight to its full unlit base or removed
native ambient illumination and used gamma-encoded light/material coefficients
as linear multipliers of decoded albedo. Encoding the result does not recover
the native gamma-space multiplication. Ambient was not uploaded twice: the
problem was treating already authored brightness as fresh reflectance or
replacing it with lighting in different units.

A controlled GPU case uses native primary 0.8, texture RGB
`(168,190,33)/204`, up normal/view, light `(0.6,0.8,0)`, RGB 0.8, white material
coefficients, roughness 1 and dielectric F0 0.04. Native RGB is `(168,190,33)`.
The saved pre-change shaders and corrected shaders produce:

| Path and native material | Profile | Before RGB | After RGB |
| --- | --- | --- | --- |
| Plain surface, unlit | Off | 210, 237.5, 41.25 | 168, 190, 33 |
| PBR, unlit | Off | 209.24, 235.95, 49.81 | 168, 190, 33 |
| PBR, ambient-only | Off | 245.66, 255, 55.46 | 168, 190, 33 |
| PBR, unlit or ambient-only | On, key 1, ambient 0.4 | 212.36, 239.87, 48.26 | 168, 190, 33 |

For example, the old unlit PBR linear result is approximately
`1.6144 * decode(C) + 0.007111`. The old profile result is approximately
`1.0144 * decode(texture) + 0.007111`: the native primary 0.8 was lost.
Neither expression has unit response to the native lit reference.
The corrected reference ratio is exactly 1 on that face.

A night case with primary 0.25 and moon RGB `(0.12,0.16,0.24)` returns its
native `(42,56,18)` in all three shader forms. These are controlled production
shader evaluations, not measurements of a rerun game frame or ROI.

## Interiors, existing profiles and validation

Static interior fixtures with `preserveSceneBrightness=false` and
`nativeAmbientStrength=0.4` retain their previous RGB exactly. For the
ambient-only fixture above, that is `(67.2,76,13.2)` in the plain evaluator and
`(125.326,142.432,20.358)` with a PBR material. This change deliberately does
not recalibrate the absolute lighting units of directly lit/static interiors.

There is no JSON/schema migration. Outdoor key intensities reduced solely to
compensate for the old gain should be reconsidered from **1x**. Ambient/fill
balance can then be adjusted for shadow depth. Saved profiles are not rewritten.

`pica_baked_lighting_gpu_tests` runs the production surface-uniform builder,
plain evaluator, specialized PBR and uber PBR: day/night, profile off/on,
preservation off/on, unlit/ambient-only ownership, walls/shadows, key intensity
and tint, camera basis, near-horizon bounds, local lights, alpha, dark channels,
disabled optional sources and unchanged static/native-fragment ambient.
The ordinary rim/effect and canonical pipeline tests remain separate.

## Live admission diagnostic

Set `TRIAEVUM_SURFACE_CALIBRATION_TRACE=1` **before starting the process**.
The value must be exactly `1`; unset or other values leave it inactive. It
writes to stderr at the end of frame 1 and every 120th presentation frame.
Each line begins `TRIAEVUM_SURFACE_CALIBRATION frame=...`:

- `kind=summary`: mode, profile scope/enabled, preservation and native gains,
  key intensity/tint/direction, ambient, AO/GI/reflection and grass settings.
- `kind=class`: counts and vertex/index element totals by domain, pass, replay,
  native classification, shader path and admission reason. Native-memory
  replays are counted separately; skipped native projected shadows have their
  own class. Draws that fail before pipeline selection are not included.
- `kind=draw`: the 24 largest scene presentation draws by `elements` (index
  count for indexed draws). Includes texture base-level hashes, geometry and
  canonical shader identities, framebuffer target, native flags and inputs,
  scene sources, effective uniforms, and actual `plain`, `PBR-GGX` or `native`
  shader path with `program=uber|specialized`. This is element ranking, not
  screen coverage; generated grass and later fullscreen effects are separate
  passes, not scene PICA draws in this list.

For the Lon Lon comparison, retain a complete sampled frame in Native,
PerPixelPBR/profile off, and PerPixelPBR/profile on at key 1. Match the ground
using `texture0`, `geometry`, target and element count. Check these fields:

1. `native`, `vertexLit`, `fragmentLit`, `nativeMaterialDiffuse`,
   `nativeDiffuse0..2`, `nativeDirection0..2` (W is slot enabled), and
   `nativeDirectSlotMask`. Nonzero coefficients with mask 0 and vertex lighting
   enabled must now classify as `ambient-only`.
2. `candidate`, `uniforms`, `calibrationUniform`, `admitted`, `reason`, `path`.
   `reason=no-sky-light` with `skyMask=0` identifies the clock-driven source
   gate. `native-fragment-lighting`, `authored-appearance`, `no-perspective`,
   `domain-not-scene`, `pass-not-world`, `no-surface-hooks`, builder rejection
   and `shader-fallback` distinguish the other admission boundaries. The
   reported reason is the first applicable failure; the flags retain the
   other conditions. `calibrationUniform=1` is the uploaded reference enable;
   `admitted=1` additionally requires an applied evaluator. For plain water,
   whole draws bypass it and mixed draws apply it only to non-water fragments.
3. `keySource=profile|native-sun-moon|native-static|native-draw`, `keyDirection`
   and `keyColorSource` distinguish the effective key policy and the native
   colour a scene-following profile borrows. `sourceColorN`/`sourceDirectionN`
   are raw world inputs; `profileRadianceN`/`sceneRadianceN` are material-weighted
   view-space shader inputs (the profile intensity is `profileDirectionN.w`).
   `referenceUp.w=1` enables calibration; `referenceKey` is the untinted unit
   profile key reference, and is zero with the profile off, which instead uses
   `sceneRadianceN` as its reference. `resolvedAmbient` and
   `environmentParameters` expose ambient/local gain policy.
4. `nativePrimarySource` describes the vertex primary's producer and
   `tintInput`/`tintEnabled`/`tintScale` its tint inputs. These are CPU inputs,
   **not an interpolated primary RGB measurement**. `tevColorSources` is a hex
   mask of final TEV RGB dependencies: bits 0/1/2 are vertex/fragment primary/
   fragment secondary, bits 3..6 are textures, bit 14 is a constant. This mask
   follows RGB dependencies, not operands selecting a previous stage's alpha.

The trace reads only captured draw data; it performs no GPU readback or game
state changes. Only selected frames format records, and only contenders for
the largest-draw list resolve texture hashes. The recorder and raw native
classification are covered by `pica_surface_calibration_trace_tests`; the GPU
test also covers the newly admitted ambient-only case in all three evaluators.
The controlled tests do not establish which draw produces the live ground ROI.

## Generated grass: terrain reference (round 33)

Grass replays the source terrain's native vertex program into a per-vertex
lighting atlas. The placement carries source-triangle indices and barycentric
weights, so every blade samples the terrain it grows on. Root/tip colours mix
with the observed terrain texture (including texture twins) at the saved
texture influence and brightness. Those settings are appearance, not emission.
The native TEV passthrough scale/rounding still applies. Receive Lighting off
keeps the explicit unlit opt-out; native fog is composed after grass shading.

The shadow-enabled atlas previously used `pica_surface_native_response` even
when the ground was admitted to the baked-reference evaluator. It therefore
replaced/added illumination in absolute profile units and lost the native
primary colour reference at night. The non-shadow atlas already called the
correct `pica_surface_primary` evaluator. The disagreement was inside the
coefficient producer, not in texture-twin matching or the day/night clock.

The coefficient producer now shares `pica_surface_baked_response` with the
ground. For native primary C, response R, horizontal reference H, and selected
shadow-casting directional contribution D, it stores:

```
raw         = C * R / max(H, epsilon) + additive_local_and_scene_rim
shadow_part = C * D / max(H, epsilon)
lighting    = clamp(raw - shadow_part * (1 - visibility), 0, 1)
```

Only the selected source is removed by the grass shadow grid; ambient, fill
and point/placed lights retain the same ownership as the terrain. Mode 5 of
the existing three-layer coefficient atlas carries this calibrated result.
No extra atlas, GPU readback, day/night heuristic or exposure constant is used.
The existing shader's source gains, orientation bounds, key reference and
colour-space convention are shared, including the native encoded primary.

Native surface-lighting mode deliberately keeps blades tied to the native
terrain primary too: generated grass is a modern addition, but should not
acquire an independent night-time light. Its atlas remains the native replay
without a modern profile response. No native terrain draw is modified.

A new interpolant carries the terrain's lit-white response, evaluated with
the same native material rounding/TEV scale. The grass-specific toon rim is
multiplied by this response. The combined tip/root colour, toon bands and rim
are capped by it before native fog. Thus high texture-tip brightness or a
bright rim cannot recreate a daylight ceiling over a dark native surface.
Scene-level local lights and rim can still illuminate both terrain and grass.

`grass_lighting_gpu_tests` executes the production coefficient producer,
recomposition, grass appearance functions and fragment colour/rim block on
Vulkan, comparing the atlas against the production ground evaluator. Its 330
cases cover day/night, unlit/ambient-only terrain, profile and preservation
on/off, partial/full shadows, slopes, twilight/dark channels, camera basis,
fill/local lights, TEV gain, fixed interiors, directly lit materials, Native
presentation, Receive Lighting off and extreme tip/rim values. It compiles all
nine grass/atlas shader variants. The existing baked-ground suite (88 cases
in three paths plus two native-fragment cases) and 110 grass/toon foundation
tests also pass.

A controlled unshadowed unit-reference day case, using the user's root/tip
colours, texture influence 0.22 and brightness 2.87/4.0, stays RGB
**(128,222,40)**: **0% change**. Holding the raw profile fixed and setting the
native reference to 0.25 changes its old night RGB **(128,222,40)** to
**(32,56,10)**, approximately 75% lower after native rounding. These are
production-shader numeric measurements, not game captures. A daytime surface
whose old absolute-profile response already exceeded its native reference
will be corrected too; the live below-a-few-percent target must be measured
with the user's daytime save and camera. No fitted day-only compensation is
introduced to conceal that mismatch.
