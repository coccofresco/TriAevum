# Animated lighting: continuous light and shadow under every day/night change

Date: 2026-10-02. Baseline: `2f53274`, branch `perf/aot-levers`.
Status: **strategy, nothing implemented**. The measurements below are real;
every design value marked *initial* is a starting point to settle by
measurement in its phase.

The [NRI architecture](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md) remains
authoritative. This plan extends the optional per-pixel lighting, the scene
environment profiles and the paged virtual shadow maps described there; it
changes nothing in Native lighting, Authentic or the F2 override.

## 1. Purpose

The game changes its lighting over time in many ways: the day/night cycle, the
Sun's Song, cutscenes that set or advance the time, scene changes that pick a
day or a night variant, weather, lightning, light changes between rooms. The
per-pixel renderer must show all of them with:

1. **Fluid motion.** Light and shadow move in every presented frame, also
   where the original moves in steps.
2. **No pops.** No visible jump of light or shadow except where the game itself
   cuts (a camera cut, a fade, a scene change).
3. **Constant cost.** A moving light never costs more per frame than a fixed
   budget.
4. **Intrinsic coverage.** Nothing in the renderer knows *why* the light
   changed. Every present and future day/night change is covered because it
   can only reach the renderer through one signal (section 4).

Decisions already taken by the user (2026-10-02):

- Motion must be fluid even where the original is stepped.
- A light below the horizon casts no shadow and its direct light is attenuated.
  Section 5.3 narrows the attenuation to sky lights, with the evidence.
- The existing scene profiles are disposable; they are recreated in the new
  profile standard (section 5.8).

## 2. Requirements

| # | Requirement | Verified by |
|---|---|---|
| R1 | While the native light moves, the light direction used for shading changes in every presented frame, with no speed discontinuity above the threshold of section 7 | light-direction series |
| R2 | While the light moves, the shadows move in every presented frame | shadow blend series, edge tracking in consecutive captures |
| R3 | No direction jump of light or shadow unless the game cut (section 4.2, class C) | jump count against the cut list |
| R4 | The shadow-casting light is never below the horizon | below-horizon frame count = 0 |
| R5 | Shadow map work per presented frame never exceeds the page budget, at any light speed | pages per frame, GPU time on moving frames |
| R6 | Day to night and night to day are continuous at any speed (normal time, Sun's Song, time-lapse cutscenes) | probes of section 7 |
| R7 | A fast native event (lightning, flash) is not smeared | colour series around the event |
| R8 | Native lighting, Authentic and F2 are byte-identical to today; the feature is inert when per-pixel lighting is off | deterministic captures |
| R9 | A scene profile works for every light state of its scene (day, night, cutscene, weather) without per-state authoring | profile probes at several times |

## 3. Evidence

### 3.1 Measured: the title intro

Boot without input, user configuration at 1280x720, 5,400 presented frames
(`qualify/intro-lights` in the work directory: `boot_probe.py`,
`boot1-light-trace.txt`, `boot1-shadow-series.json`, `boot1-sheet.png`,
`boot1-flip.png`). The title scene is `0x6B` and has no profile, so the native
environment lights drive per-pixel lighting and the virtual shadow map.

- The scene has two environment directionals: `L0` (sun) and `L1` (moon),
  **exactly opposite** at every frame. Directions are signed bytes; colours
  change by one unit every few frames.
- **Stepped motion.** The main light moves in 74 of 900 guest frames, in steps
  up to 0.68 degrees, and stays still for 14 frames (median) between steps.
- **Pop at dawn.** The shadow light is "the brightest". The sun becomes
  brighter than the moon while it is 20 degrees *below* the horizon: in one
  frame the shadow direction turns by 180 degrees (dot product -1.0, presented
  frames 584 and 1184).
- **Shadows from below the ground.** After that flip the shadow light stays
  below the horizon for 1,317 of 3,800 frames (about 22 of 63 seconds).
- **Full shadow-map redraw at every step.** Each direction change redraws 215
  of 220 resident pages, static ones included: GPU frame 6.5 -> 9.8 ms mean,
  15.5 ms maximum, on 273 frames. Interpolated presentation creates two or
  three intermediate directions per native step, each a full redraw.
- A cutscene time skip moves the direction by 15 degrees in one frame (a cut).
- The next demo scene (`0x17`) has a light **from below** on purpose: an orange
  light, direction `(-63, -127, -31)`, under a lava-lit room.

### 3.2 The engine's light state machine

From `code.bin` (EUR), the environment update around `0045DFD8..0045EEE0`
(`r4` = environment context, `sl` = save context). Addresses and offsets are
read from the disassembly; the role of each field is inferred from its use and
is confirmed against a live trace in P0:

| Address | Fact |
|---|---|
| `0045DFD8..0045DFE0` | The clock (`save context + 0xC`, 16 bits) advances every frame by twice a value loaded just before (the time speed) |
| `0045E068..0045E078` | **Time mode** when `env + 0x20 == 0` and `env + 0xA7 == 0xFF`; otherwise **settings mode** (`0045EA58`) |
| `0045E0A4..0045E430` | Time mode: colours are blended between the entries of a time table (`env + 0x21` selects it; `env + 0x22` and `+0x23` are read in the same loop, a second table and its enable) and truncated to bytes |
| `0045E438..0045E4A0` | Time mode: the sun direction is recomputed from the clock: `x = -sin(t) * 120`, `y = cos(t) * 120`, `z = cos(t) * 20`, **truncated to signed bytes** at `env + 0xB5..0xB7` |
| `0045E4A4..0045E4C0` | Time mode: the second light is the exact negation (`env + 0xBB..0xBD`) |
| `0045EA58..` | Settings mode: light setting `env + 0xA5` (28-byte records: ambient, light 1 direction and colour, light 2 direction and colour, fog), optionally blended from `env + 0xA6` with the weight at `env + 0xC8` when `env + 0xA4` is set |

Consequences:

- **Every** lighting change goes through this one update and ends in the same
  small state: two directionals (direction, colour), ambient, fog. The adapter
  already captures the two directionals from their live records
  (`NativeA32LightingProbe::CaptureSceneLights`).
- The steps of the sun are a truncation, not a property of the source: the
  clock has 65,536 positions per turn (0.0055 degrees).
- The engine itself knows whether its lights are **sky lights** (time mode) or
  **authored fixed lights** (settings mode). No heuristic can replace this: a
  fixed pair such as `(72,-72,72)` / `(-72,72,-72)` is also exactly opposite
  (the nightmare cutscene, 3.5).

The read-only decompilation evidence (`<oot3d-decomp>`, never copied) confirms
that cutscenes carry set-time and lighting commands; both act on the state
above.

### 3.3 Offline smoothing prototype

`qualify/intro-lights/smooth_prototype.py` on the recorded trace (guest frames
301-1201 and 1908-2423):

| Reconstruction | Frames moving | Step per frame | Largest change of step | Distance from the ideal path |
|---|---|---|---|---|
| Native bytes | 74 of 900 | up to 0.68 deg | 0.68 deg | - |
| Critically damped spring, 1.5 rad/s | 892 of 900 | 0.032 deg | 0.0016 deg | 1.30 deg |
| Critically damped spring, 3 rad/s | 892 of 900 | 0.033 deg | 0.0055 deg | 0.65 deg |
| Critically damped spring, 6 rad/s | 892 of 900 | 0.034 deg | 0.022 deg | 0.32 deg |
| Ramp between steps | 892 of 900 | 0.033 deg | 0.042 deg | 0.24 deg |

A filter makes the motion uniform at the price of a lag that grows with speed.
The exact clock (3.2) gives the same uniformity with no lag; the filter is
therefore the fallback, not the main path (5.2).

### 3.4 What the renderer does today

- `ResolvePicaDirectionalShadowLightSelection` and
  `FindPicaSceneMainDirectional`: the shadow light is the brightest directional,
  wherever it points.
- `BuildVirtualShadowLayout`: any direction change larger than float roundoff
  invalidates every page.
- `PicaEnvironmentLighting`: a profile adds a key light with a constant colour
  and intensity; the user's two profiles mute the native direct light, so only
  the native ambient (30% / 50%) follows the game.
- `ResolvePicaEnvironmentKeyDirection` (commit `9669831`): the key direction can
  follow the main scene light, so it inherits the flip and the steps.
- Scene lights are paired by source identity for interpolated presentation.
- Native light changes do not reset temporal history; only a profile change
  does (`mFrameEnvironment`).

### 3.5 P0 probes (2026-10-02)

`TRIAEVUM_SCENE_LIGHT_TRACE=1` prints, for every guest frame whose native
environment light state changed, the scene, the mode (`light_mode`, `override`),
the light setting and its blend, the clock, both directional records and
whether the records equal the clock-derived bytes (`clock_match`).
`qualify/intro-lights/p0_analyze.py` classifies each guest frame by section
4.2. Probes in `qualify/intro-lights` (`run_p0.sh`, `run_p0_save.sh`):

| Probe | Scene, mode | Clock | Finding |
|---|---|---|---|
| `p0-boot`: title intro, no input | `006B` time | +3 or +6 per guest frame | 2,423 of 2,423 frames match the clock; demo scene `0017` is settings mode (`light_mode=1`, `override=9`) |
| `p0-field`: Hyrule Field from noon, 3,800 guest frames | `0051` time | +7 per update (+14 when a draw is skipped); one day is about 9,400 updates | 3,800 of 3,800 match; dusk handover at `CAE4` with the sun 12 degrees below the horizon; 192 frames below |
| `p0-dawn`: field from 02:00 (state saved by `p0-field-night`) | `0051` time | as above | handover at `36AE` (05:08) with the sun below the horizon; 165 frames below |
| `p0-house`: Kokiri, into Link's house | `0055`, `0034` time | stopped (`8000`, noon) | time mode with a still clock: class S; the scene change is a cut |
| `p0-kakariko-night`: structural scenario `spot01_info_entry_00db` from the 02:00 field state | `0052` time | stopped | towns keep their time; the moon is the main light |
| `p0-dream`: new-game nightmare (`link_dream_start`) | `0051` settings | `E000`, not used | lights constant over 827 frames; the brightest light (`72,-72,72`) comes **from below**, the other from above; the blend runs between two identical settings |

Every time-mode frame of every probe satisfies `clock_match`: the published
direction is the clock's, with no other writer. The fixed-time places measured
so far are time mode with a still clock, not settings mode; settings mode was
met in cutscene scenes. Not yet traced: the Sun's Song (needs an ocarina input
sequence), weather, lightning, and a blend between two different settings.

Reproducing a probe from a state saved at internal scale 1000 with the
Enhanced (DLSS) settings exposed a regression of `2f53274`: the restore applied
the settings' scale first, and the saved targets no longer matched. The restore
now uses the saved scale, or the settings' own scale when the two agree; a
state saved at another scale restores as before that commit, and the first
frame replaces its targets.

## 4. Governing principle: one signal, classified by shape

### 4.1 The single choke point

The renderer consumes one typed snapshot of the native light state per guest
frame. Section 3.2 shows that every mechanism of the game writes that state.
The design therefore never asks what happened in the game; it classifies the
**shape** of the signal. A new or unlisted gameplay situation cannot escape,
because it has no other way to change the lights.

### 4.2 Signal classes

| Class | Shape of the signal | Treatment |
|---|---|---|
| S | Still: nothing changes | One shadow layout, single sampling; today's cost |
| A | Slow continuous motion of direction | Exact or reconstructed continuous direction (5.2); shadow keyframes with continuous dissolve (5.5) |
| B | Fast continuous motion (several degrees per second) | Same path; the shadow page set shrinks so the budget and the spacing hold (5.5) |
| C | Cut: a jump beyond what motion explains, or a declared discontinuity | Snap every filter; rebuild the shadow layout in that frame (as today) |
| D | Energy only: colours, ambient, fog change, direction fixed | Colours follow, dequantised when stepped, untouched when fast (5.2); no shadow work |
| E | Handover: the shadow-casting light changes identity | Continuous by the horizon model for sky lights (5.3); timed fade otherwise (5.4) |

A change can belong to several classes at once (dawn is A + D + E).

Cut detection uses evidence, never the cause:

- scene identity or environment context changed;
- presentation continuity epoch changed (state load, renderer target reset);
- the engine switched between time mode and settings mode;
- the direction turned by more than `cut angle` in one guest frame (*initial*:
  8 degrees; the Sun's Song probe of section 7 must stay below it);
- a colour changed by more than `event step` units in one guest frame
  (*initial*: 3): passed through unfiltered, not a cut for the shadow map.

### 4.3 Coverage of the game's day/night and light changes

"Measured" means traced in section 3.1. "Expected" means derived from the
engine structure of section 3.2 and to be confirmed by the probe named in
section 7; none of these rows needs its own code.

| Situation | Engine path | Classes | Status |
|---|---|---|---|
| Time passing in open areas | time mode, clock advances | A + D + E at dawn and dusk | **measured** (`p0-field`, `p0-dawn`) |
| Sun's Song where time flows | time mode, clock advances fast | B + D + E | expected (P-song-flow) |
| Sun's Song where time is fixed | fade, scene reload at the new time | C | expected (P-song-fixed) |
| Title intro, attract demos, credits time-lapses | time mode driven by cutscene | A/B + D + E, C at camera cuts | **measured** (`p0-boot`) |
| Cutscene set-time command | clock rewritten | C (or A if below the cut angle) | measured once (15 degree skip) |
| Cutscene lighting command, with or without blend | settings mode, blend weight | D, A for blended directions, C without blend | settings mode **measured** (`p0-dream`, constant); a blend between different settings expected |
| Entering or leaving an area that picks a day or night variant | scene load | C | **measured** (`p0-kakariko-night`) |
| Gate and drawbridge sequences at dusk and dawn | time mode plus cutscene | A + D + E, C at cuts | expected (P-field) |
| Fixed-time scenes (towns, interiors) | time mode, clock stopped | S | **measured** (Kokiri, Link's house, Kakariko at night) |
| Light settings changing between rooms | settings mode, blend | D, A | expected (P-interior) |
| Weather onset and clearing | time mode, table blend | D | expected (P-weather) |
| Lightning, flashes, boss effects | colour adjustments | D with event pass-through | expected (P-intro-new-game) |
| Warps, death, continue, loading a save | scene load | C | scene loads **measured** as cuts |
| Loading a TriAevum state | continuity epoch | C | measured mechanism (state load exists) |
| Pause, dialogue, menus | clock stops | S | expected |

## 5. Design

### 5.1 Typed scene light snapshot (OOT3D adapter)

`PicaSceneDirectionalLight` gains, per light:

- `DirectionTowardSource`: the best native direction available (5.2);
- `SkyLight`: true when the engine is in time mode (3.2). Title semantics stay
  in the adapter; the shared renderer sees only the typed flag;
- unchanged: `SourceIdentity`, `Color`.

`PicaSceneDirectionalLights` gains a `ContinuityEpoch` that the adapter
advances on scene change, environment re-initialisation and mode switch, so the
renderer's cut detection (4.2) does not read title state.

The title already publishes its axes (`WorldUp`, `WorldAzimuthZero`) for the
profile; they move to the snapshot so that scenes without a profile have them.

### 5.2 Continuous reconstruction

The published snapshot is the continuous one. Everything downstream (surface
lighting, shadow map, grass, GI) reads the same values; nothing downstream
filters again.

**Sky lights: exact direction from the clock.** In time mode the adapter
recomputes the direction of 3.2 in floating point from the 16-bit clock, and
checks it: truncating its own result must reproduce the three native bytes.
If the check fails (another writer changed the record), it publishes the native
bytes and the dequantiser below applies. No lag, no parameter, exact at any
clock speed: classes A and B share one path.

**Everything else: dequantiser.** Fixed-mode directions that blend between
light settings, and all colours, are integer signals. Per channel:

- the output is a critically damped spring toward the native value;
- its rate follows the cadence of the native steps (*initial*: time constant =
  1.5 step intervals, at least 3 rad/s), so the lag stays about one and a half
  quantisation steps at any speed instead of growing with it;
- the output never leaves a band of 1.5 units around the native value;
- a change above `event step` (4.2) passes through at once: lightning stays
  sharp, a blend that already moves every frame is not delayed;
- a cut (4.2) snaps.

The filter runs once per guest frame with the fixed guest time step, so runs
are deterministic. Its state is not saved: a state load is a cut.

**Presented frames.** The existing identity-paired interpolation samples the
continuous snapshots for each presented frame; a sky direction interpolates the
clock angle, not the truncated vector.

### 5.3 Horizon model

With `up` the published world up and `e` the elevation of a light
(`sin e = dot(direction, up)`):

- **Shadow eligibility (all lights).** A light with `e <= 0` never casts the
  shadow map.
- **Shadow strength (all lights).** `shadow(e) = smoothstep(sin 1 deg, sin 8 deg, sin e)`
  (*initial*): shadows lengthen and fade toward the horizon instead of ending.
- **Direct light (sky lights only).** `direct(e) = smoothstep(sin -4 deg, 0, sin e)`
  multiplies the light's direct term: full at and above the horizon, as the
  title lights its sunrise and sunset, none from 4 degrees below. (A first
  window, -1 to +4 degrees, also dimmed the sun just above the horizon and
  darkened the title's sunrise; P1 captures.)

Why the attenuation is limited to sky lights: a sun below the horizon is
hidden by the ground, but fixed-mode scenes place lights below the horizon on
purpose (the lava glow of scene `0x17` in 3.1, the common fill from below).
Removing those would darken interiors against the art. The engine's own mode
flag separates the two cases exactly (3.2).

Effect on the day/night handover: sun and moon are opposite, so exactly one is
above the horizon. As the sun sets its shadows fade to zero at the horizon,
where the moon's start from zero. Dawn and dusk are continuous at any speed
with no special case; the brightness crossover that causes today's flip no
longer selects anything.

### 5.4 Main light and handover

- `effective energy = luminance(colour) * direct(e)` for a sky light,
  `luminance(colour)` otherwise; zero for any light with `e <= 0` as a shadow
  candidate.
- The shadow-casting light is the eligible light with the largest effective
  energy, with hysteresis: a challenger takes over only above the incumbent by
  a margin (*initial*: 10%).
- When the identity changes outside a cut, the shadow strength fades out, the
  identity swaps at zero, and it fades in (*initial*: 0.4 s in total, guest
  time). For the sky pair the horizon model already holds the strength at zero
  at the swap, so the fade is invisible; the rule matters for fixed-mode pairs
  whose colours blend across each other.
- `FindPicaSceneMainDirectional` stays the single owner of the rule, shared by
  surface lighting, the shadow pass and the profile (5.8).

### 5.5 Shadow keyframes

A light that moves in every frame would invalidate the whole virtual shadow map
in every frame (3.1: +3 to +9 ms). Instead:

**Layouts.** Up to three shadow layouts exist per target: two *displayed*
(`K[n-1]`, `K[n]`) and one *in construction* (`K[n+1]`). Each is a complete
virtual shadow layout for one fixed light direction, with its own static depth
atlas, its own residency and its own region of the composed atlas. A still
light keeps one layout and a composed atlas of one region, as before; a moving
light uses three regions side by side.

**Construction under a budget.** Each presented frame draws at most `B` static
pages of `K[n+1]` (16 at the measured day and night speeds; one page measured
at about 0.015 ms), in the existing residency priority order, and restores them
into the layout's region as they are drawn. The layout's direction is the
light's direction when its construction starts. Because a slot keeps its
region, the first display of a constructed layout composes only the pages that
dynamic casters reach: a rotation moves no page.

**Continuous dissolve.** Receivers sample `K[n-1]` and `K[n]` and blend the two
visibilities with a weight that advances every presented frame from 0 to 1.
A dissolve lasts a whole number of frames, 1.25 times the construction time of
its layout: the margin absorbs a construction that takes a little longer, and
the whole number makes every frame advance the displayed shadow by the same
angle. When the dissolve ends, the completed `K[n+1]` becomes the newest layout
and the next construction starts at the light's direction of that frame. In
steady state the displayed shadow moves at the light's angular speed, two
dissolve periods behind it (0.6 degrees at the title intro's speed).

**Spacing law.** With `P` pages in a layout, light speed `v` per frame and
budget `B`: keyframe spacing `delta = v * 1.25 * P / B`. The budget rises from
16 up to 64 pages so that `delta <= 0.5` degrees; above that speed the spacing
grows and the dissolve reads as a blur of the sweep. (The first design shrank
the page set under fast sweeps; raising the budget is simpler and bounded at
about 1 ms. The set can still shrink if the Sun's Song probe asks for it.)

**Start and stop.** After a cut, or when a still light starts to move, the
displayed shadow starts from the exact layout, holds for one construction and
reaches the light's speed over two dissolves: the lag builds up without a
jump. When the light stops, the remaining keyframes play out and the last
layout is built at the light's final direction.

**Still light (class S).** No construction starts; receivers sample one layout,
as before. After 600 frames without motion the other layouts are released, and
the composed atlas returns to one region once slot 0 is the displayed layout
(every cut puts the layout in slot 0).

**Cut (class C).** A new continuity epoch, another layout geometry (resolution,
range) or a turn above 8 degrees in one frame: slot 0 is rebuilt in that frame,
as before. Cuts coincide with camera cuts, fades or scene loads.

**Dynamic casters.** Each displayed layout recomposes its dynamic pages every
frame, as one layout does when the light is still.

**Receivers.** The receiver block holds two layouts (matrix, clip levels, page
table), the dissolve weight and each layout's atlas offset; the atlas binding
is unchanged. The second layout is read only while the dissolve flag is set;
without it its bytes are neither uploaded nor bound (the range ends before
them). Receivers' lights are matched against the scene's current light, not
against a layout's direction, so the lag never unbinds a receiver. A fragment
looks its visibility up once: the lighting asks for it once per shadowed term
of each matched light and used to repeat the whole filtered lookup each time.

**Memory.** While the light moves: three static atlases and a composed atlas of
three regions, 96 MiB at 2048 against 32 MiB for a still light (384 against 128
at 4096). The resolution is not lowered for a moving light: the shadow would
change sharpness when the light starts to move. A device whose largest image
is narrower than three regions keeps one layout and relayouts it in steps of
0.25 degrees.

**Why not fewer layouts.** With two layouts, display and construction must
alternate, and the shadow would move in visible bursts. The alternative that
reaches two layouts without bursts is listed in section 10.

### 5.6 Sampling at the presentation rate

Light direction, colours, horizon factors, handover fade and dissolve weight
are all evaluated per presented frame. The guest rate (and the interpolation
multiplier) does not show in the motion.

### 5.7 Temporal consumers

- **TAA / DLSS / FSR.** Native light changes keep not resetting history. A
  colour event (4.2) above a threshold raises the reactive mask for that frame
  so a flash does not trail; to verify in P4.
- **MXAO temporal.** Independent of light; no change.
- **Brixelizer GI.** Its history weight drops with the change of key and
  ambient energy between frames, and resets on a cut; P4, GI is optional and
  off in the reference configuration.
- **Grass.** Consumes each draw's surface-lighting uniforms and the shadow
  grid; it follows without its own logic. The shadow grid samples the same two
  layouts and weight.
- **Point-light shadows.** Unaffected.
- **Fog and sky.** Native, already animated; the profile's fog strength scales
  them.

### 5.8 Profile standard, version 6

A profile no longer replaces the scene's light with a constant one. It
**regrades the native lights**, so it inherits every native change and one
profile serves every state of its scene (R9).

```json
{
  "schema": "triaevum.scene-environment",
  "version": 6,
  "sceneId": 85,
  "enabled": true,
  "sources": [
    {"gain": 2.0, "tint": [1.0, 0.96, 0.9]},
    {"gain": 1.2, "tint": [0.8, 0.88, 1.0]}
  ],
  "shadowDirection": {"mode": "source", "azimuthOffset": 0.0, "elevationOffset": 0.0},
  "ambient": {"nativeGain": 1.0, "addedColor": [1.0, 1.0, 1.0], "addedIntensity": 0.0},
  "nativeLocalGain": 1.0,
  "fogStrength": 1.0,
  "addedLight": null,
  "pointLights": [
    {"enabled": true, "intensity": 1.0, "radiusScale": 1.0, "colorTint": [1.0, 1.0, 1.0]},
    {"enabled": true, "intensity": 1.0, "radiusScale": 1.0, "colorTint": [1.0, 1.0, 1.0]}
  ]
}
```

- `sources[i]` regrades native environment light `i`: in time mode the sun
  (0) and the moon (1), so day and night have independent looks; in settings
  mode light 1 and light 2. The light's colour is `native * tint * gain`,
  after the horizon model.
- `shadowDirection` applies to whichever light casts the shadows (5.4):
  `source` with a fixed azimuth and elevation offset, or `otherAxis` (the axis
  of the other light, on the caster's side; today's `scene_ambient`). The
  energy always follows the native elevation; the offset direction is clamped
  above a minimum elevation (*initial*: 3 degrees) so an offset never pushes
  the caster below the ground.
- `ambient.nativeGain` scales the native ambient (which follows the game);
  `added*` is an optional constant term.
- `addedLight` is the optional constant light for art direction (today's
  authored key in its fixed form): direction, colour, intensity, shadowless.
  It does not follow the game, by definition.
- Versions 1 to 5 still load, as today, and behave as they do today; saving
  writes version 6. The user's two profiles are recreated, not migrated.

F1 **Environment** remains the only owner. The Key light tab becomes **Scene
lights** (per-source gain and tint, shadow direction) and **Added light**.
New profiles start from gains of one: enabling a profile changes nothing until
the user moves a control.

### 5.9 Ownership and inertness

- Title addresses, the time-mode flag and the clock formula live in the OOT3D
  adapter (`oot3d_native_a32_lighting`). The dequantiser, horizon model, main
  light rule, handover and shadow keyframes are reusable mechanisms in the
  shared 3DS renderer and the shadow pass.
- Native lighting, Authentic, F2 and native-memory rendering never read the
  snapshot; they stay byte-identical (R8).
- Settings: `delta_max`, the page budget and the horizon angles are renderer
  constants, not user settings, until a measurement shows a need.

## 6. Phases

Each phase ends with its metrics of section 7 measured before and after, the
deterministic captures, the structural diagnostics, and the F1 smoke test where
F1 changes.

### P0. Instruments and probes

- Promote the temporary light trace to a diagnostic
  (`TRIAEVUM_SCENE_LIGHT_TRACE`): native bytes, mode flag, clock, published
  values, class decided for the frame.
- Add to the frame diagnostics: light direction used, shadow weight, pages
  built for the layout in construction, displayed layouts.
- Resolve the save-context address and the time-speed value read at
  `0045DFD8`; confirm the mode flag against the trace.
- Build the probes of section 7 that need input or a state (field, Sun's Song,
  town, interior, new-game intro).
- **Exit:** every row of 4.3 has a recorded trace and its class.

### P1. Continuous light

- Snapshot fields of 5.1; exact sky direction and dequantiser of 5.2.
- Horizon model (5.3), main light and handover (5.4), in
  `FindPicaSceneMainDirectional`, surface lighting and the shadow selection.
- Tests: dequantiser unit tests on the recorded trace (uniform speed, bounded
  band, event pass-through, cut snap); horizon and handover unit tests;
  composition tests for the adapter's clock check.
- **Exit:** R1, R3, R4, R6 for light; R7; R8. Until P2, the shadow map keeps
  its layout until the light has turned 0.25 degrees from it, so a light that
  moves in every frame does not redraw every page in every frame.

**Done (2026-10-02).** `renderer3ds/pica_scene_light_continuity.h`
(`PicaSteppedSignal`, `PicaSceneShadowCasterTracker`), horizon functions and
the main-light rule in `pica_light_binding.h`, the adapter's
`ContinueSceneLights` (clock direction, ramps, cuts, caster decision, trace
`TRIAEVUM_SCENE_LIGHT_CONTINUITY`), surface lighting (sky direct horizon),
shadow selection and pass (published strength, 0.25 degree layout step),
presented-sample interpolation (no interpolation across an epoch, strength
interpolated), visual replay V20 (sky flag, up, epoch, caster, strength).
Probes (`qualify/intro-lights`, `p1_analyze.py`):

| Probe | Moving frames | Largest step | Jumps | Casting below the horizon | Largest strength change per frame | Handovers |
|---|---|---|---|---|---|---|
| `p1-boot` | 2,421 of 2,423 time-mode frames | 0.034 deg | 0 | 0 (was 1,317) | 0.007 | 1, at strength 0 (sunrise) |
| `p1-field` | 3,799 of 3,799 | 0.078 deg | 0 | 0 (was 192) | 0.017 | 1, at strength 0 (dusk) |
| `p1-dawn` | 1,999 of 1,999 | 0.078 deg | 0 | 0 (was 165) | 0.017 | 1, at strength 0 (dawn) |
| `p1-dream` | lights still | - | 0 | 0 | 0 | 0; the light from above casts |

The step of the native clock changes once per transition, not every frame:
+7 per update by day and +14 by night in Hyrule Field (time runs twice as
fast at night), +6 then +3 in the title intro. These single speed changes are
the largest change of step (0.039 degrees, with the sun at the horizon); no
clock smoothing is needed.

With the user's Enhanced configuration and profiles (Kokiri and Link's house,
time stopped at noon), P1 against P0 is 20/20 identical over the house entry
and exit captures (the first-run outlier at frame 40 aside, as before). Tests:
5 continuity tests in the foundation suite (314/314), the adapter's clock path
in the composition tests, the visual replay round trip, scene-frame 20/20.

**Key colour (2026-10-02, ahead of P3).** A scene-following key (`scene_main`,
`scene_ambient`) takes the main light's colour and brightness, its authored
colour as a tint and its intensity as a gain (`ResolvePicaEnvironmentKeyColor`;
composition test; F1 smoke 7,988 assertions). Reported by the user: with a
title-scene profile the key stayed white at intensity 1 through night and
sunrise. A profile that zeroes the native ambient keeps its constant added
ambient at night; P3's ambient gain addresses that.

### P2. Continuous shadows

- Three layouts per target, budgeted construction, receivers with two layouts
  and a weight, spacing law and page-set selection, memory rule (5.5).
- Grass shadow grid on the same layouts.
- Tests: layout rotation and budget unit tests; still-light captures identical
  to the single-layout path; structural diagnostics for the declared histories.
- **Exit:** R2, R5, R6 for shadows; GPU time on moving frames within the noise
  of still frames plus the measured two-layout sampling cost.

**Done (2026-10-02).** `renderer3ds/virtual_shadow_keyframes.h` (the scheduler:
slots, whole-frame dissolve, budget, cuts), `VirtualShadowMapCache`
(construction mode: budgeted static pages composed as they are drawn; page
selection made cheaper with the same results), the receiver shader and block
(two layouts, weight, atlas offsets, one lookup per fragment), the shadow pass
(three layout slots with their static atlases, one composed atlas of one or
three regions, release after 600 still frames), the grass shadow grid on the
same block, diagnostics `nri_vsm_keyframe_*` and `nri_vsm_pending_pages`.
Probes at 1280x720 on the development GPU (`qualify/intro-lights`,
`p2_analyze.py`), P1 build against P2 in interleaved rounds:

| Probe | Displayed step / light step | Lag | Pages updated per frame | GPU frame, P1 | GPU frame, P2 |
|---|---|---|---|---|---|
| Title intro, 2,600 frames, 2 rounds | 1.000 (p50); 43 held and 39 ramp frames after the 2 cuts | 0.62 deg | p50 30, p95 52 (P1: 225 every 16 frames) | mean 5.55; held frames 5.35, relayout frames 8.5 | mean 5.6, p95 6.6 |
| Field through sunset, 4,400 frames | 0.999-1.000 in every frame, none stalled | 0.71 deg | p50 28 | p50 5.4-5.5 on held frames, p95 12.8 | p50 4.7-5.1, p95 6.5-7.5 |
| Field at night and dawn (before the single lookup) | 0.999-1.000 | 1.0 deg (the night clock runs twice as fast; budget 22) | p50 28, p95 64 | mean 2.8-2.9 without the upscaler | mean 3.1-3.2 without the upscaler |

- The construction never exceeds its budget; it costs 0.2 ms of GPU time for
  16 pages. A rotation moves no page: 46 consecutive captures at sunset across
  three rotations change by 0.38-0.45 levels on the rotation frames, inside
  the 0.36-0.51 of the other frames (the grass in the wind).
- The second lookup first cost 0.8 to 1.3 ms by day in the field (GPU p50 7.3
  to 8.0 ms): the lighting repeated the filtered lookup for every shadowed
  term. With one lookup per fragment the two layouts cost less than P1's one
  layout did, and a still light gains the same saving.
- CPU: the render thread's draw time rises by about 0.4 ms per frame while the
  light moves (6.6 to 7.0 ms p50 in the title intro): three page selections
  and caster bindings per frame instead of one. A page selection fell from
  65-88 to 52-74 microseconds. P1's relayout frames no longer exist.
- Still light, the user's configuration at 3840x2160 over the Link's house
  entry and exit: 19 of 20 captures identical to P1 (the twentieth differs
  between two runs of one build, as before). Native lighting 12/12 and
  directional shadows off 12/12 identical to P1 on the Kokiri walk.
- Validation on the title intro reports the same messages for P1 and P2 (a
  descriptor at binding 26 never updated, vertex outputs missing for fragment
  inputs 7-10, descriptor types at bindings 1-3 of one program); they predate
  this phase. Fixed 2026-10-05: they came from the hybrid shader preparation's
  startup links and the unwritten PBR seam binding
  (`TRIAEVUM_STUTTERING_RESOLUTION_STRATEGY.md`, "Valid startup links and the
  seam binding"); the intro now runs without validation errors.
- Tests: scheduler (still light, even motion at two speeds, stop, cuts, the
  single-layout fallback), construction budget and composition, the exact
  overlap rule of the page selection against a brute-force reference, receiver
  packing; foundation 323/323, scene-frame 20/20, pipeline cache 51/51,
  composition and visual replay suites, pass artifacts 70, renderer boundary
  title-neutral (99 files).

The nightmare (fixed light settings) keeps one layout and 32 MiB over 1,627
frames, with a cut at each of its three scene cuts. Not probed yet: the Sun's
Song (the budget rises to 64 pages; above that the spacing grows) and a
weather change.

### P3. Profile standard

- Version 6 schema, loader for 1-5 unchanged, F1 tabs, panel smoke test.
- Recreate the Kokiri and Link's house profiles with the user.
- **Exit:** R9; a profile probed at day, night and a cutscene state.

### P4. Events and temporal consumers

- Reactive mask on colour events; GI history response; DLSS/FSR checks on
  lightning and on the dawn/dusk sweep.
- New-game intro probe (lightning, cutscene light settings).
- **Exit:** R7 on real events; no trailing in captures after a flash.

### P5. Optional refinements

- Warped single-layout lookup (section 10) if the dissolve shows double edges
  on long shadows or the memory rule is too tight.
- Per-light-setting art direction, only if regrading by source is not enough.

## 7. Verification

### Probes

| Probe | Scenario | Covers |
|---|---|---|
| P-boot | Boot, no input, title intro (exists) | A, D, E at dawn, C at cuts |
| P-field | State in an open area, time running through dusk and dawn | A, D, E both ways |
| P-song-flow | Sun's Song in an open area | B, E at speed |
| P-song-fixed | Sun's Song in a fixed-time area | C |
| P-town | Enter and leave a town at day and at night | C, S |
| P-interior | A room change with different light settings | D, A in settings mode |
| P-weather | A weather change | D |
| P-intro-new-game | New game opening, scripted menu input | D events, settings-mode cutscene lighting |
| P-state | Load a state taken at another time of day | C |

### Metrics and thresholds

| Metric | Before (P-boot) | After P1 | After P2 | Target |
|---|---|---|---|---|
| Guest frames in which a moving light moves | 74 of 900 | all | all | all |
| Largest change of angular step between frames | 0.68 deg | 0.039 deg, once per native speed change | unchanged | <= 0.01 deg outside cuts and native speed changes |
| Direction jumps outside cuts | 2 (the 180 degree flips) | 0 | 0 | 0 |
| Frames with the shadow light below the horizon | 1,317 of 3,800 | 0 | 0 | 0 |
| Pages rendered in one frame by a moving light | 215 of 220 | 215 of 220, every 0.25 deg | 16 static pages (22 at night) plus dynamic pages; p95 52-64 updated | <= budget plus dynamic pages |
| GPU frame time on moving frames against still frames | 9.8 against 6.5 ms, 15.5 max | relayout frames 8.5 against 5.35 ms | 5.6 ms on every frame (title intro); below P1 by day in the field | within noise plus two-layout sampling |
| Shadow motion | steps of the native bytes | steps of 0.25 deg | the light's step in every frame; ramp after a cut | monotonic, even |
| Native lighting and shadows-off captures | - | not read by these modes | identical to P1 | byte-identical to the baseline |

All timing comparisons use interleaved rounds; the GPU is shared on the
development machine.

## 8. Cost budget

| Item | Cost |
|---|---|
| Snapshot, clock direction, dequantiser | negligible CPU, once per guest frame |
| Layout construction while the light moves | 16 pages per frame by day, 22 at night (up to 64): 0.2 ms GPU measured for 16 |
| Second displayed layout while the light moves | one more filtered lookup per shadowed fragment; with one lookup per fragment per layout the total is below P1's |
| Dynamic casters while the light moves | twice the dynamic pages of a still light (about 6 to 9 per layout measured) |
| Render-thread CPU while the light moves | about +0.4 ms per frame (three page selections and caster bindings) |
| Still light | one lookup per fragment instead of one per shadowed term; otherwise as before |
| Memory at 2048 | 96 MiB while the light moves, 32 MiB still |

## 9. Risks

| Risk | Mitigation |
|---|---|
| The dissolve shows a double edge at the tip of long shadows | Spacing law keeps the offset inside the filter width near the caster; the horizon fade removes the longest shadows; P5 warp |
| Two-layout sampling costs more than expected at 4K | Measured in P2: one extra filtered lookup per shadowed fragment, more than paid for by looking up once per fragment. A stochastic choice of layout stays rejected: without temporal anti-aliasing it shows as stipple on the edges of tall casters |
| Another writer changes the sky record (cutscene) | The clock check of 5.2 falls back to the native bytes and the dequantiser |
| The Sun's Song step exceeds the cut angle | P-song-flow sets the threshold; the clock speed read in P0 bounds it |
| A fixed-mode scene relies on a light from below as its main light | It still lights; it only never casts the shadow map (5.3). Reviewed per probe, not per scene |
| Memory at 4096 | 384 MiB while the light moves (5.5); the resolution is the user's setting and is not lowered |
| Shadows trail the light by about half a second | 0.6 to 1.0 degrees at the measured speeds; a faster light raises the budget and shortens the construction |

## 10. Alternatives considered

- **Redraw the whole shadow map every frame.** Exact and simplest; +3 to +9 ms
  per frame measured. Rejected by R5.
- **Hold the map and swap at intervals.** Constant cost but stepped shadows.
  Rejected by R2.
- **Two layouts with alternating construction and dissolve.** Shadows move in
  bursts. Rejected by R2.
- **One layout with a warped lookup.** The receiver shifts its lookup by the
  occluder distance times the angle between the layout's direction and the
  light's; two layouts suffice (one displayed and warped, one in construction).
  Lower memory and single sampling, but approximate at depth discontinuities.
  Kept as the P5 refinement, to be judged against real double edges.
- **Filter only (no clock).** Works (3.3) but lags in proportion to speed and
  needs tuning for the Sun's Song. Kept as the fallback path of 5.2.
- **Profiles per light setting or per time of day.** Covers only the states
  someone authored. Rejected as the base by R9; possible later as art
  direction (P5).
- **Attenuate every light below the horizon.** Removes authored interior
  lighting (3.1). Rejected; the engine's mode flag decides (5.3).

## 11. Open points to settle by measurement

1. `cut angle`, `event step`, hysteresis margin, handover time (4.2, 5.4).
2. Horizon angles for `shadow(e)` and `direct(e)` (5.3), judged on dusk and
   dawn captures.
3. Page budget and `delta_max` (5.5): settled at 16 to 64 pages and 0.5
   degrees for the measured speeds; the Sun's Song probe may still move them.
4. Layout size rule at 4096 (5.5): settled, the resolution is not lowered.
5. Whether colours need the dequantiser at all once gains are applied (5.2).
