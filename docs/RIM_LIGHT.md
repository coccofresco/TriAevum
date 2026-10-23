# Rim light

The default rim is an **independent virtual light**, fixed to the camera. It
needs no scene lights, ambient color or shadow visibility. Its added radiance
is the same in an unlit interior and daylight for the same settings, normal
and view. The final image still includes ordinary surface lighting, material
composition, fog and post-processing.

`SceneEnvironmentRuntime` is the sole owner. The one F1 group is **Lighting >
Lights & Shadows > Scene lights > Rim light**, saved with the scene's day/night
profile. Both the plain per-pixel and PerPixelPBR material paths implement it.
The latter uses the final normal-mapped, seam-adjusted normal. Settings:

- **Source**: Independent (default), or Scene lights for the previous direct
  light response. Switching retains the independent controls' saved values.
- **Color**: independent RGB, each channel 0..1; default warm white `[1, .96, .9]`.
- **Intensity**: 0..8, default 1. No division by grazing angle or light count.
- **Sharpness**: 1..16, default 4. Higher values narrow the edge.
- **Azimuth**: -180..180 degrees in camera coordinates. 0 lights the top of the
  silhouette, 90 the right, -90 the left, and either 180 the bottom. The source
  is always behind the subject; this selects its side around the view axis.
- **Coverage**: 0..1, default .25. Zero uses only the directional lobe; raising
  it fills the remaining silhouette, reaching an even full rim at 1.
- **Scope**: Actors (default), or Actors and world. Enable is on in the built-in
  base profile. Intensity zero also produces zero rim uniforms.

For normalized view-space shading normal `N` and surface-to-camera direction
`V`, the independent term is:

```text
F = clamp(1 - abs(dot(N,V)), 0, 1) ^ sharpness
D = (sin(azimuth), cos(azimuth))
S = smoothstep(-0.25, 0.75, dot(normalize(N.xy), D))
rim = color * intensity * F * mix(S, 1, coverage)
```

Degenerate normals/views give no Fresnel response; a zero projected normal
uses S=0 without normalizing it. Every factor except intensity lies in 0..1,
so the virtual source is bounded by `color * intensity`. It has no distance,
scene-color, native-gain, material-AO or shadow multiplier. PBR adds it directly
to linear lighting after scene evaluation. The plain path adds it to the
primary surface response before native tint/TEV composition. It is excluded
from directional-shadow capture and GI source radiance in both paths.

Scene lights retains the previous cheap per-light weight:
`intensity * F * smoothstep(-.25, .5, dot(L,-V)) * clamp((dot(N,L)+.5)/1.5, 0, 1)`.
This multiplies each evaluated light's existing shadowed radiance, preserving
its attenuation, cone/area terms and gains. Unshadowed lights remain unshadowed.
Color, azimuth and coverage are disabled in this mode. The old ambient rim is
removed: the independent source supplies the requested interior response
without a second ambient-dependent system.

The OOT3D adapter keeps the existing actor attribution and opaque-draw policy:
no UI, atmosphere, transparent/additive or fading passes, projected shadows,
authored unlit appearance or isolated lighting. World scope is opt-in. There
are no extra passes, resources or pipeline variants, and no draw-order, alpha,
blend, depth or stencil changes. Off zeroes all rim uniforms; disabled edits
canonicalize to unchanged renderer inputs. Modern scene profiles remain
bypassed by Native/NativePBR, Authentic, F2 and Use native environment.

The optional version-5 `rimLight` object remains backward readable; no version
bump is needed for optional fields. A missing object stays off. A round-27
object missing the new fields takes Independent, warm white, azimuth 0 and
coverage .25, retaining its saved enable/intensity/sharpness/scope. Its old
`ambient` is range/type-checked on read, ignored, and omitted on save. New
fields also receive strict type, finite/range and enum validation. Save/reload,
night profiles and hot reload share this codec. Different profiles can still
author different rims; day/night illumination itself never drives Independent.

Claude can merge this object into the play-kokiri profiles while preserving
all other settings (none of those files were edited here):

```json
"rimLight": {
  "enabled": true,
  "source": "independent",
  "color": [1.0, 0.96, 0.9],
  "intensity": 1.0,
  "sharpness": 4.0,
  "azimuth": 0.0,
  "coverage": 0.25,
  "scope": "actors"
}
```

Verification uses owner codec/persistence tests, adapter/uniform tests, actual
F1 widgets (all RGB channels), production surface equations on Vulkan and both complete PBR
shader bodies. GPU cases cover dark interiors, all scene-source kinds and
shadow states, distance, cardinal directions, coverage, color, maximum gain,
sharpness, degenerate inputs, off and GI isolation. No game is run by Codex.

Verified 2026-10-10: `TriAevum.exe` and touched targets build; environment tests
pass 92 checks, pipeline tests 54/54, foundation 352/352, GPU 121 cases and two
PBR shader compilations, and focused F1 smoke 1,978 assertions. Full F1 still
fails before this panel at the unchanged material-editor texture-identity
assertion (see the F1 review). No game or play-kokiri profile was touched.

Claude's lab should use `I:/_triaevum_work/devwarp/host-build/savestates`: compare
fixed-pose daylight and dark-interior captures, with all real lights/ambient
zeroed and with full key/local shadows. The independent rim must remain.
Rotate the camera; check top/right/left/bottom azimuths, coverage 0/.25/1,
sharpness 1/4/16, color and intensity 0/1/8, normal-mapped actors and both surface
paths. Switch to Scene lights and confirm shadows affect that mode. Check
actor/world scope, fades, UI/particles, F2 restoration, save/reload and night
transitions. Watch clipping at high intensity and measure GPU cost.
