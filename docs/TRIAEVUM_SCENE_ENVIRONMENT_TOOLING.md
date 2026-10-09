# Scene environment profiles: tooling for agents

Scene environment profiles (`environments/scene-XXXX.environment.json`, and
`scene-XXXX.night.environment.json` for a scene's night) are edited by people
in F1 and by agents as files. Both go through one codec,
`oot3d_scene_environment_codec.{h,cpp}`, used by the runtime, the F1 editor and
the tool below; the shape and ranges are documented in
`docs/schemas/triaevum.scene-environment.v5.schema.json`.

## triaevum_environment_tool

Developer builds (`TRIAEVUM_DEVELOPER_TOOLS`), target
`triaevum_environment_tool` (not built by default). It never starts the game.

```text
triaevum_environment_tool validate <file>...
triaevum_environment_tool format [--check] <file>...
triaevum_environment_tool explain <file>
```

- `validate` prints one JSON line per file:
  `{"file", "valid", "sceneId", "timeOfDay", "errors": [{"pointer", "message"}]}`.
  Every value is checked and every problem reported at its JSON pointer
  (`/placedLights/1/intensity: is 30; must be a number within 0..16`); a
  file named `scene-XXXX[.night].environment.json` must name that scene and
  time of day. Files are read as the runtime reads them (64 KiB at most).
  Light ids, the version and the scene id are whole numbers (an integral
  `5.0` is accepted); light ids 1..4294967295, unique in the profile.
- `format` rewrites a valid file in the runtime's canonical form (the bytes
  F1 saves: two-space indentation, numbers in their shortest exact form, a
  final newline), atomically as the runtime saves; `--check` only reports
  files that are not canonical.
- `explain` prints the authored profile, what the renderer receives (key and
  fill directions toward their sources, ambient, native strengths, each
  visible placed light with its emission axis, colour times intensity, reach,
  beam or extents, its identity as a hexadecimal string), the scene
  point-light groups, what depends on the scene at run time (a key light that
  follows the scene's main light takes its direction, colour and brightness
  there; the authored direction is then only its fallback, with the offsets
  in degrees) and notes: hidden lights, lights without shadows that light
  through walls, the eight point-shadow cube maps shared with Navi and the
  fairies, the 16 placed lights per scene, that the graphics mode and the
  device decide which of these apply, units and angle conventions.

Exit status: 0 success, 1 an invalid or non-canonical file, 2 usage.

## Hot reload

`TRIAEVUM_ENVIRONMENT_HOT_RELOAD=1` makes the running game re-read the current
scene's profile files when they change on disk. Only developer builds contain
the watcher. It looks at both the day and the night file (size and time) at
most every 250 ms and acts on a change once two looks agree, so a file still
being written is not read; a file rewritten with the same content is no
change. Each outcome is a stderr line:

```text
TRIAEVUM_ENVIRONMENT_RELOAD scene=0x0055 time=day result=applied file=scene-0055.environment.json
```

- `applied`: the file validated and is now the profile in use.
- `kept-invalid`: it did not validate (`detail=` gives the first error); the
  profile in use stays, so a half-written file never reaches the renderer.
- `kept-missing`: the file is gone; the profile in use stays.
- `kept-unsaved`: F1 has unsaved edits; they are never replaced. The change
  waits: F1 "Reload" takes the files as they are then (finding again which
  file the time of day uses).

Creating or removing the night file switches which file the night uses; the
selection and its profile change together, only when the file validates (an
invalid night file leaves the day file in use and as the file F1 saves). The
game's own saves are not picked up as changes, and acknowledge only the file
they wrote. Each profile in use has a revision: an F1 edit made on a profile
that a reload has since replaced is refused, not written over it.

## Identity

The renderer identifies a placed light by tag, time of day, scene and the
whole 32-bit light id (`PlacedLightIdentity`), so day and night lights and high
ids never share point-shadow history.

## Verification

- `triaevum_f1_settings_smoke` (9365 assertions): canonical text and exact
  round trip, a night profile refused as a day one, identities distinct across
  time of day, scene and the high bit, every error of a broken profile at its
  pointer (fractional id, out-of-range intensity, wrong-length colour,
  missing field, repeated id), `explain` showing the renderer's placed light
  and the scene-following key.
- `triaevum_environment_tests` (developer build, 17 checks, a temporary
  directory and a synthetic clock): an invalid file kept, a valid one
  applied, the same content not re-applied, nothing read before two looks
  agree, unsaved edits kept and taken by Reload, an edit on a replaced
  profile refused, its own save not reloaded, an invalid night file leaving
  the day file selected, a night file created and removed, a missing file
  kept.
- The tool on the 26 profiles of the play configuration: all valid;
  integral `5.0` versions and scene ids accepted, `null` light entries
  reported at their pointers.
- Live, developer build, free camera above Kokiri Forest with hot reload: an
  invalid write is `kept-invalid`, the valid write 1.5 s later is `applied`
  and the green omni light turns red in the next frames (right third of the
  frame, mean RGB 89/126/22 -> 108/109/24), gizmo included.
