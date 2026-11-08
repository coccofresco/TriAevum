# Texture and material source qualification

## Scope and ownership

This records the adoption of the previously uncommitted texture/material work,
separately from the surface-lighting ownership increments. It includes:

- **Native + PBR**: authored materials use native lights; unsupported/unmapped
  draws and projected shadows retain native rendering. Scene Environment and
  VSM overrides are inactive without discarding their saved preferences. F2
  temporarily restores Native for this mode.
- **Material editor**: one always-available F1 owner for selected-material
  parameters, image imports, packed/separate ORM and transient diagnostic views.
  Lighting retains the model selector; Textures retains pack folder/loading.
  Obsolete global `Graphics.Effects.PBR` multipliers are removed, not silently
  baked into existing descriptors. The graphics persistence schema is 14.
- **Image and descriptor publication**: editor imports share the asynchronous
  pack worker. Imported/generated PNGs are content-addressed; the descriptor is
  the atomic commit point. Errors preserve the draft and previous usable bundle.
  CPU and GPU publication retain complete material bundles, including optional
  base color. Numeric previews are isolated by native Azahar material identity.
- **Material response**: normal convention/strength, roughness floor/glossiness,
  dielectric F0, full specular weight, independent screen-space reflection weight
  and bounded height/parallax. Height does not displace geometry, silhouettes or
  depth; native alpha is replayed from the original coordinates.
- **Reflection consumers**: the shared material-guide B channel encodes local
  reflection gain, not a material class. Hi-Z applies receiving-pixel gain after
  its bilateral filter; SSSR applies it in material resolve. The Hi-Z shared-tile
  filter retains the 5x5 kernel and handles partial workgroups.
- **Auxiliary UV seam softening**: optional, material-owned blending of selected
  normal/ORM/emission/height channels across supported native topology. Base
  color is not blended. Hard/open/non-manifold edges, separate submissions and
  unsupported UV contracts are not joined. Native primitive order and alpha
  remain authoritative; topology and pose have separate cache ownership.
- **Shared plumbing**: typed affine UV and posed-geometry evaluation, immutable
  frame-arena seam data, Vulkan/NRI descriptor agreement, texture catalog native
  identities and the shared texture selector. The Grass affine-UV consumer and
  shadow geometry use these shared helpers rather than duplicate decoders.

Detailed authoring fields and renderer contracts are in
[the architecture document](OOT3D_NRI_FIDELITY_EXTENSION_ARCHITECTURE.md#native-per-pixel-lighting-and-material-packs-2026-09-23).
Actual-widget workflows and historical campaigns are in
[the F1 review](TRIAEVUM_F1_MENU_REVIEW.md).

## Isolation

The commit selection contains source, tests, build declarations and documentation
only. Material packs, imported images, captures, checkpoints and title DLLs remain
private. Independent AO/GTAO, Grass placement/lighting, audio, gameplay pacing and
AOT memory changes are not included. Mixed files are staged by reviewed hunks;
the original working tree is not replaced with the isolated copy.

Verification exports the exact index into a private source tree and builds it
with separate binaries and archives. This checks the selected changes against
the committed tree rather than accidentally relying on unrelated working-tree
changes. The user installation, material packs and running game are not inputs
that the verification may modify.

## Production GPU verification

Built from the isolated staged sources on an RTX 3060:

- PBR topology and rendering: smooth manifold seams, hard/open/disconnected and
  non-manifold rejection; rotated/mirrored tangent frames; overlapping bands and
  three-chart corners; linear emission and height; exact base/alpha preservation;
  zero strength and unselected channels; indexed strips, degenerates and base
  vertices.
- Hi-Z bilateral and SSSR material resolve: zero, neutral, 2x and fractional
  receiving-pixel weights, including adjacent reflective pixels and partial
  workgroups. Temporal reactivity is not used as coverage.
- All 24 scene-lighting GPU cases pass with the selected material integration.

These exercise production shaders, not source-string snapshots. The SSSR test
qualifies its material resolve kernel, **not** the complete SSSR provider scenario.
The previously documented full-provider resource-lifetime failure remains an
explicit qualification limit; it is not hidden by a successful kernel test.

## CPU and actual-widget verification

The isolated build passes:

| Suite | Result |
| --- | --- |
| Graphics foundation, including texture packs and persistence | 294 tests |
| PICA shader/pipeline cache | 42 tests |
| PICA scene frame | 19 tests |
| Native presentation policy | Pass |
| Actual F1 renderer/Controls/TopScreen widgets | 6,378 assertions |

The F1 count belongs to this selected tree. It is not the larger working-tree
count that also includes unrelated, uncommitted controls.

## Deterministic runtime verification

Ten independent runtime launches use a private forest checkpoint and private
material pack, 1280x720 output, native 30 Hz without interpolation, 32 frames
and a framebuffer capture at frame 30. AO, Grass and screen-space reflections
are disabled for this campaign; their providers are not qualified by it.
Native + PBR is used except for the two explicit Native bypass cases.

| RGB comparison | Changed pixels |
| --- | ---: |
| Working runtime Off versus isolated runtime Off | 0 |
| Seam Off versus enabled with zero strength | 0 |
| Native with seam setting Off versus On | 0 |
| Seam Off versus On, strength 1 and width 0.05 | 17,916 |
| Width 0.05 versus 0.15 | 50,501 |
| Material baseline versus height scale zero | 183,810 |
| Material baseline versus normal scale zero | 216,743 |
| Material baseline versus minimum roughness 0.8 | 138,364 |

The parameter comparisons establish observable response, not an artistic
reference or a performance claim. Native, material-baseline and seam-enabled
framebuffers were also inspected directly.

Live checkpoints for material Off/On and Native On compare exactly against
Native Off for guest memory, native PICA frontend/submission, NativeMemory
render targets, presentation depth and presentation alpha. These checkpoints
contain no stencil payload; no live stencil comparison is claimed.

All ten launches complete with zero Vulkan/NRI validation errors and zero
failed passes, missing reads, unresolved passes or missing physical resources.
Each reports 11 Vulkan warnings, matching the working-runtime baseline, and
zero NRI warnings. This is not a warning-free validation claim.

The qualified isolated `TriAevum.exe` SHA-256 is
`e46057b94ea754e36eb8ae490067a5ceca864ffbe8abb9f6fed585e051031974`.
Per-suite logs and binary receipts are in `test-receipts.json`; capture
comparisons are in `runtime-qualification.json`; native-state and validation
comparisons are in `structural-qualification.json` under the evidence root.

## Evidence

Private source-build, GPU and runtime evidence belongs under
`<work>/material-commit-qualification/`. Earlier material-editor,
material-weights and material-seams campaigns remain under their existing
`<work>/` directories. Their historical measurements must not be
presented as fresh measurements of a different source tree.

## Isolated build

The full Windows runtime and its owning CPU suites compile successfully from
the selected source tree with Clang 22.1.6, MSVC 14.44 headers/libraries and
Vulkan SDK 1.4.350.0. No unrelated AO, audio or AOT implementation was needed
to close the material dependencies.

The runtime build targets are `oot3d_native_game`,
`oot3d_graphics_foundation_tests`, `oot3d_pica_shader_pipeline_cache_tests`,
`oot3d_pica_scene_frame_tests` and `native_presentation_policy_tests`.
Texture-pack and settings-persistence cases belong to the foundation executable,
not separate CMake targets. The standalone GPU project is
`tools/renderer/tev_program`, with `pbr_seam_gpu_tests`,
`reflection_weight_gpu_tests` and `scene_point_light_gpu_tests`.

For UI changes, run `tools/triaevum_release/tests/run_f1_settings_smoke.ps1`
against the same build and archive directory. Runtime capture recipes retain
the exact executable, arguments, private configuration and validation settings
in each capture's `invocation.json`.
