# iMMERSE MXAO

MXAO from Pascal Gilcher's iMMERSE, ported to TriAevum's NRI renderer. This
directory is a module under its own licence (`LICENSE.md`): all rights remain
with the author, the original iMMERSE licence (`LICENSE-iMMERSE.txt`) applies,
and TriAevum uses it under the author's explicit permission, granted
exclusively to the project. The renderer it plugs into is described in
[`OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md`](../../../../../docs/OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md).

`mxao_shader.{h,cpp}` ports MXAO from iMMERSE (revision `f57d3af`).
`mxao_module.cpp` is the module's shared library (`triaevum_immerse`): MXAO's
NRI execution and the texts of its F1 controls, behind the module interface
`module_interface/triaevum/immerse_module.h`. TriAevum loads the library when
it is installed beside the runtime; its `fast/oot3d/mxao_pass.cpp` serves the
module the renderer's device, textures, programs and barriers, and
`fast/oot3d/mxao_settings_panel.cpp` draws the controls. The library is built
only with `THREE_DS_RECOMP_ENABLE_IMMERSE` (see `LICENSE.md`, Boundary).
The graph contract is GTAO's: native FP32 depth and the coverage-bearing normal
guide in, the published R32F visibility out, composited before UI. By default there is no history; the
optional [temporal mode](#temporal-mxao) accumulates it over frames.

- Kept from the original: 4x4 depth deinterleaving, the Low-IDGAF slice/step
  presets, per-tile jitter, the ground-truth, solid-angle and visibility-bitmask
  estimators with the original thickness heuristic, and the two-iteration
  depth-guided regression filter (F1 filter quality 0-2).
- Changed: the renderer's exact projection replaces ReShade's assumed 60-degree
  field of view; the renderer's normals are selectable besides the depth-derived
  ones (see [Original options](#mxao-original-options-and-debug-views)), with the
  RGB8 bound of the GTAO kernels; fade uses native distances; each slice is
  normalized against its own unoccluded integral and occlusion (not visibility)
  is stored.
  `mxao_gpu_tests` failed before the last two changes: the ground-truth estimator
  darkened an empty RGB8 plane by 0.163, and stored visibility lost one truncated
  half-precision step on every estimator.
- F1 **Lighting** adds **iMMERSE MXAO** to the provider list with MXAO quality,
  method, radius, amount (above 1 the original gamma response) and filter quality;
  fade start/end sit under Advanced AO. The settings object `AO.Mxao` was added
  without a schema change.
- Single 600-frame runs on the Kokiri reference walk, 2560x1440, RTX 3060: the
  AO scope median is 1.13 ms for multiscale GTAO Medium, 2.96 ms for MXAO
  visibility bitmask Medium with one filter iteration, and 5.07 ms for High with
  two. MXAO works at full resolution. Validated runs report zero Vulkan/NRI
  errors.

## Temporal MXAO

F1 **MXAO temporal accumulation** (`AO.Mxao.Temporal`, default off) accumulates the
per-pixel occlusion over frames with the scene's motion vectors. It is not part of
the original; it lives with the port in `fast/immerse` and runs between the
occlusion and the regression filter, which then filters the accumulation.

- The AO pass declares a `MotionVectors` read and depends on `Motion`, so the
  native boundary that composes AO also derives that frame's motion (the backend
  runs the motion block before AO); presentation reuses it for TAA or upscalers.
  The setting enables the draws' motion guides as GI does. Off, the plan, the
  shaders' arithmetic and the output are unchanged (byte-identical captures).
- Each frame adds a bit-reversed phase (`MxaoTemporalPhase`) to every pixel's
  interleaved jitter, so a pixel visits all sixteen slice directions and step
  offsets in sixteen frames.
- The history (two RGBA16F images written alternately: occlusion, signed depth,
  length) is read at the texel under `uv + motion`. It is rejected where the
  motion marks a disocclusion or the stored depth differs from this surface
  point's depth in the previous view: `MxaoPreviousDepthRow` maps MXAO's view
  position through `Previous * inverse(Current) * Projection` to the previous clip
  w, with a tolerance of 2% plus the local depth slope (the smaller one-sided
  step, so silhouettes do not widen it). The history is clamped to the range of
  this frame's 4x4 window, which holds all sixteen phases, and blended with weight
  1/length; the length grows to 16 and, while moving, is capped at
  `16 / (1 + pixels per frame)`, never below 4. A camera cut, a temporal reset, a
  non-consecutive frame or changed MXAO settings restart it.
- Bilinear history taps with a fixed length of 16 were measured first: on the
  Kokiri walk with a pronounced AO (radius 24) they raised the High preset's
  error to an Extreme reference from 0.0207 to 0.0247 by blurring fine grass
  occlusion. The nearest texel with the moving cap brings it to 0.0217 (the cap
  alone 0.0218, the nearest texel alone 0.0236, a fixed length of 8 0.0232).
- Measured effect (AO term = capture / AO-off capture of the same frame): a
  static Kokiri view flickers 32% less from frame to frame (0.00142 -> 0.00096)
  with the same mean; while walking, fine-scale variation drops 6% (High) to 9%
  (Medium). Accumulation does not recover the occlusion a higher preset finds:
  the visibility bitmask unions the sectors of one frame's steps, so averaging
  sparse frames converges to the sparse estimate (Medium stays 0.049 from
  Extreme). It is a stability option, not a substitute for a higher preset.
- Cost in the field (1280x1067 target, RTX 3060, medians of three 600-frame
  runs, A/B/B/A): the GPU frame goes from 3.16 to 4.06 ms. The AO scope grows
  from 0.85 to 1.28 ms, the boundary's motion vectors add 0.11 ms and the native
  draws 0.35 ms for the motion guides they now write.
- Attribution of the AO share (temporary switches, removed): a temporal frame
  that rotates the jitter phase but skips the accumulation costs what the
  non-temporal AO does (0.85 ms), and an accumulation that only copies the
  current estimate costs almost as much as the full one. The cost is the extra
  full-resolution pass (its RGBA16F history write and the filter reading it), not
  the rotation, the reprojection or the history tests. The temporal program
  reads its group's raw estimate once into an 11x11 shared tile (depth slope and
  the 4x4 clamp window) instead of up to twenty fetches per pixel: identical
  output (`mxao_gpu_tests` metrics and field/Kokiri captures byte-identical) and
  1.275 -> 1.25 ms for the AO scope (A/B/B/A). Run-to-run variation of the game
  probes is about +-0.15 ms on this machine, so smaller differences need A/B/B/A.
- `mxao_gpu_tests` cover an invalid history (identical to the non-temporal
  filter input), a static view (frame-to-frame change 11x lower, slightly closer
  to Extreme), depth rejection, the neighbourhood clamp and motion reprojection
  with a revealed column. Diagnostics: `mxao_temporal_pass_count`,
  `mxao_temporal_history_valid_count`.

## MXAO original options and debug views

Every option of the original is exposed (F1 **Lighting**, iMMERSE MXAO), and the
debug views show what the renderer passes to MXAO and the terms of its estimate.

- **Normals** (`AO.Mxao.Normals`: `Geometry` default since 2026-10-03, `Depth`, `Renderer`). The original's
  `MXAO_USE_LAUNCHPAD_NORMALS`: from depth, the four neighbour cross products
  weighted against the depth steps they span, exactly as the original (at the
  image border the clamped neighbour is replaced by the mirrored opposite one);
  or the renderer's normal guide. Only the depth normal is the plane of the
  depth samples MXAO tests. The guide holds shading normals: interpolated vertex
  normals, which across a sharp corner tilt up to half the corner angle from
  both faces, PBR normal maps and AO texture relief; and the interactive grass
  writes no normal, so its blades carry the ground's normal underneath. Where
  the guide tilts from the depth surface the surface rises above its own tangent
  plane and occludes itself: on the Kokiri walk (user settings: solid angle, Low,
  filter 2, temporal, radius 1.54, amount 2) depth normals brighten 6-15% of each
  frame by more than 8 levels (mean +1.3 to +3.4 luma) and darken under 1%,
  mostly the grass blades, creases and Link. `mxao_gpu_tests`: a plane tilted 39
  degrees under a facing shading normal keeps 0.77 visibility with renderer
  normals and 1.0 with depth normals. Renderer normals reproduce the previous
  build byte for byte (12/12 captures). AO texture relief bends only the
  renderer normals.
  `Geometry` (2026-10-03, F1 "Face normals"): the face normal of the visible
  surface from the geometry guide (G channel), the exact plane of the
  rasterized triangle, so the plane of the depth samples without the
  reconstruction's neighbour selection across creases and silhouettes; from
  depth where the guide has none. Selecting it (or a face-normal debug view)
  is what publishes the guide and declares it as the AO pass's input
  (`EffectsReadFaceNormal`); otherwise nothing changes (Kokiri walk, MXAO from
  depth: 12/12 captures identical to the previous build). `mxao_gpu_tests`:
  on planes face and depth normals give the same visibility (0 difference on
  the 16.7 degree plane, 6.5e-7 mean on the steps), and without a face normal
  the result is the depth normal's bit for bit. Default since the user's review
  in the Kokiri Forest (2026-10-03; the Enhanced preset too). Cost on the
  user's profile (RTX 3060, interleaved 420-frame Kokiri walks, two each): GPU
  frame median 9.25-9.28 ms with depth normals, 9.76-9.89 ms with face normals
  (+0.5-0.65 ms): the native scene writing the guide +0.16 ms, the AO pass
  reading it +0.18 ms, grass unchanged; the remaining 0.2-0.3 ms falls outside
  the profiled scopes (not yet attributed).
- **Sample quality** adds the original's IDGAF preset (8 slices, 20 steps).
- **Shading rate** (`AO.Mxao.ShadingRate` full / half / quarter): 50% or 25% of
  the pixels, by deinterleaved tile parity, are estimated per frame; the others
  keep their previous estimate (stale while moving, as in the original). The
  pattern is the even one of the original's pixel-shader path. A frame after the
  estimate image is created always runs at full rate.
- **Increase radius with distance** (`AO.Mxao.IncreaseRadiusWithDistance`,
  `ScreenRadius` 0.5-10, default 2.5): a constant screen radius of 3% of the
  width per unit, along each slice's screen direction; the world radius at the
  pixel's depth drives the falloff and the bitmask thickness. Otherwise the
  native world radius (`AO.Radius`).
- **Original fade** (`AO.Mxao.OriginalFade`, `FadeDistance` 0-1): the original's
  `saturate(1 - d^2) * exp2(-(d / distance)^2)` over depth normalized by the far
  plane, instead of the native fade start and end.
- **Debug view** (not saved; `OOT3D_GRAPHICS_MXAO_DEBUG=1..12` for captures):
  ambient occlusion (the original's Show Raw AO), unfiltered occlusion (the filter
  input, the accumulation when temporal), normals in use, renderer normals, depth
  normals (view space: X right, Y up, Z toward the viewer), normal disagreement
  (black equal, white 90 degrees apart), depth over the fade range, the filter's
  edge weight, fade, temporal history length, face normals (geometry guide) and
  face vs depth normals (grey: the angle over 10 degrees, as a packed colour so
  small angles keep 8 bits; magenta: no face normal). A debug program
  (`BuildMxaoDebugShader`) replaces the resolve and writes the view into the R32F
  visibility: scalars in [0, 1], normals as 8/8/7 bits in the mantissa of a float
  in [1, 2) (`AmbientOcclusionDebugShaderLibrary`); the scene composite shows the
  output texel for texel in place of the image, before UI. Off, the composite's
  arithmetic is unchanged.
- An active debug view never reaches scanout as occlusion: the scanout path
  (effects not composited) leaves AO out while one is selected.
- Debug views on the Kokiri walk (`crops-mxao/` receipts): the renderer normals
  show the ground's normal under the grass blades, the depth normals each
  blade's own; the disagreement view is white on the blades and grey on the
  smooth-shaded cliff and Link. Validated run (depth normals, packed normal
  view): no Vulkan/NRI error, the 11 baseline warnings. Cost: AO scope medians
  of interleaved 360-frame runs (user settings, 2560x1440, RTX 3060) are 3.88 /
  4.08 ms with renderer normals, 4.58 / 4.00 ms with depth normals and 4.27 ms
  for the previous build: the run-to-run spread exceeds any difference.
- The temporal history restarts when the normal source, radius mode, fade mode
  or relief changes. Settings were added to `AO.Mxao` without a schema change;
  files without them get the defaults and are rewritten.
- Tests: `mxao_gpu_tests` (depth normals on tilted and sloped planes with all
  filters, IDGAF, screen radius 1 / 2.5 / 10, original fade, quarter and half
  shading rates against exact references, renderer / depth / in-use normal views,
  disagreement equal to the slope angle, depth and fade views), settings
  validation and persistence, and the F1 actual-widget smoke (7,783 assertions).
