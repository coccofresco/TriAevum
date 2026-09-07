# OOT3D F1 Settings Ownership Audit

## Rule

Each setting has one model owner, one F1 editor and one runtime consumer.
F1 is an editor surface only: it must not add a second behavior path.

| Tab | Owner | Runtime consumer | Apply policy |
| --- | --- | --- | --- |
| Renderer | `GraphicsSettingsRuntime` | NRI presentation/effect graph | live, capability-validated, persisted |
| Grass | `InteractiveGrassSettings` | NRI geometry provider | live, persisted; saved preset is independent |
| Textures | `AzaharTexturePackSettings` | isolated texture-pack runtime | toggles live; paths commit explicitly |
| Controls | `NativeControlConfigRuntime` | SDL-to-CTR input adapter | live preview; JSON save explicit |
| TopScreen 2.1.1 | `TopScreenUiConfigRuntime` | TopScreen UI/camera adapter | live preview; JSON save explicit |

## Closed conflicts

- Relative mouse free-camera motion no longer passes through the C-stick
  dead zone, clamp, smoothing, aim speed or aim inversion.
- Native aim and free-camera select their physical sources independently.
- TopScreen camera zoom composes with the free-camera orbit; global renderer
  FOV and TopScreen camera FOV remain separate controls.
- Mouse deltas are drained while UI owns the pointer, and capture transitions
  cannot inject a stale camera jump.
- Input and TopScreen controls remain usable as session previews even without
  persistent JSON paths.
- Display rollback is ticked by the F1 window, independent of the selected tab.
- Visual presets preserve output mode, resolution, VSync, FOV, texture-pack
  paths, grass source identities/preset and reflection assignments.
- MSAA enters with a valid sample count. NRI-only features are disabled when
  their concrete backend owner is unavailable, and validation corrections are
  displayed instead of being silently reverted.
- The reflection texture catalog is evaluated only while its collapsed editor
  is open; inactive Renderer frames no longer snapshot and preview it.

## Regression gates

- `oot3d_native_a32_input_tests`: source priority, relative mouse units,
  independent C-stick transform, calibration and live preview.
- `oot3d_top_screen_mod_profile_tests`: relative-input activation, free-camera
  orbit, zoom composition and persisted TopScreen policies.
- `oot3d_graphics_foundation_tests`: capability gates and preservation of
  presentation/content identities across presets and Authentic mode.
