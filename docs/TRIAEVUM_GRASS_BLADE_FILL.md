# Grass blade fill: whole and fading passes, camera-facing ribbons

At 4K the grass blades cost what their pixels cost. On the Kokiri Forest F5
state (Enhanced settings, 1049 blades/m², 500k budget, 3840x2160) the blade
draws took 3.40 ms; at render scale 0.5 they took 1.50 ms, and five instead of
three near segments made 0.13 ms of difference. About 2.5 ms was fill and
0.9 ms vertex work. A distant substitute cannot reach that cost (the card LOD
of `TRIAEVUM_GRASS_CARD_LOD.md` saved 12% at its default and stays off). Two
changes act on the fill.

## Whole and fading passes

The blade fragment program discarded fragments for two reasons only: the LOD
stipple of a fading blade (`lod_visibility < 1`) and the cutout of a distant
tuft. A `discard` anywhere in a program forfeits the early depth test, so every
hidden blade fragment was shaded and wrote six targets before the depth test
rejected it. Most blades never fade.

Raster-depth blade batches (`GrassDepthIsRasterDepth`) now draw twice:

1. **Whole pass**: the batches in reverse order (most segments, the nearest,
   first) with `interactive_grass_*_whole.frag`, the raster-depth program with
   both discards compiled out (`GRASS_WHOLE_BLADES`). The push constant carries
   `kGrassWholePassFlag`; the vertex program drops, before any shading, every
   instance that is a tuft or has `lod_visibility < 1`.
2. **Fading pass**: the batches in their order with the raster-depth program
   and `kGrassFadingPassFlag`, which drops the instances the whole pass drew.
   Tuft batches and explicit-depth batches draw once, as before.

The partition is the vertex program's own `lod_visibility`, evaluated
identically in both passes, so each blade is drawn exactly once by the
program it drew with before minus a discard that could not fire. Only the
order of equal-depth fragments of different blades changes (the depth test is
less-or-equal): a deterministic 4K capture differs from the previous build in
23 of 8.3 million pixels beyond the run-to-run noise (water reflections off).

A batch whose segment count only occurs below the whole-blade distance has no
fading blades and skips the fading pass; its whole pass draws every instance
without the drop flag, so a blade can never be lost between the passes.
`GrassWholeBladeDistance` is the distance below which every non-tuft blade has
visibility exactly 1 (density retention start, tuft start, draw fade start);
`GrassSegmentsMaximumDistance` the distance from which no root draws with a
segment count. With the Enhanced values the five-segment band ends at 729.7
units and blades are whole up to 730.2, so the most expensive batches draw
once. A relative margin of 1e-4 covers the rounding of the selection and
vertex distance evaluations (same eye, same anchor floats).

## Camera-facing ribbons by default

Near blades used two crossed planes. `Appearance.CrossedBlades` (F1 > Grass >
Performance > "Crossed blades (two planes)", persisted, absent in older files
= off) now defaults off: every blade that is not a distant tuft is one ribbon
whose width axis faces the camera before the blade twist, the scheme of the
reference grass renderers (Ghost of Tsushima, the Unity URP instancers) and
half the vertices and fill of the crossed pair. The maintainer approved the
look on 2026-10-20. The LOD policy (`GrassLodPolicy::CrossedBlades`, CPU and
GPU selection, residency and selection cache keys) chooses one plane for every
bin when it is off.

## Measurements

Developer launch, Kokiri F5 state, 4K, ABBA phases of 180 frames, two
interleaved sessions per build, p50 (p95):

| Build | Blades | Grass GPU total |
| --- | --- | --- |
| Previous (crossed, discard) | 3.37-3.44 ms (3.97-4.14) | 3.90-4.00 ms |
| Whole/fading passes, crossed | 2.92-3.01 ms (3.53-3.63) | 3.58-3.66 ms |
| Whole/fading passes, ribbons (default) | 2.30-2.36 ms (2.83-3.01) | 2.84-3.03 ms |

An upper bound without any discard was 2.61-2.71 ms with crossed blades; the
remainder is the second vertex invocation of the instances of bins that need
both passes. Splitting the instance lists in the GPU selection would remove
it.

## Validation

- `oot3d_graphics_foundation_tests`: program sources (no `discard;` in the
  whole program), split bits, whole-blade distance and segment bands against
  the LOD resolver, default plane count; settings persistence round trip and
  older files; selection cache key.
- `grass_lighting_gpu_tests` compiles every blade fragment variant, including
  the whole programs, and runs the colour/rim/ceiling block of
  `grass_write_fragment`.
- `renderer_spirv_pack_tests`: 99 built-in pass programs.
- F1 smoke: the crossed-blades toggle.
- Deterministic captures and the ABBA lab above.
