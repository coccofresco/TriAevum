# Scene environment profiles: tooling for agents

Scene environment profiles (`environments/scene-XXXX.environment.json`, and
`scene-XXXX.night.environment.json` for a scene's night) are edited by people
in F1 and by agents as files. Both go through one codec,
`oot3d_scene_environment_codec.{h,cpp}`, used by the runtime, the F1 editor and
the tool below. Scene saves retain version 5; group standards use version 6.
The shapes and ranges are documented in
`docs/schemas/triaevum.scene-environment.v5.schema.json` and
`docs/schemas/triaevum.scene-environment.v6.schema.json`.

## Group standards (round 34)

The adapter derives the group from the selected setup's authored scene and
room commands, using `NativeRoomIsInterior` first, then room time command
`0x10`. Live light-mode/override flags do not classify the group.

| Group | Stable file ID | Authored classification after the interior gate |
| --- | --- | --- |
| Indoor | `indoor` | Existing interior gate accepts the room |
| Outdoor, time flows | `outdoor-time-flows` | Time is retained; speed is neither 0 nor 0xFF |
| Outdoor, time at entry | `outdoor-time-at-entry` | Time is retained; speed is 0 or 0xFF |
| Outdoor, fixed time | `outdoor-fixed-time` | Both hour and minute are set (not 0xFF) |

The first readable, currently installed room defines the whole scene visit.
Later rooms cannot change it. This deliberately makes mixed Forest Temple
(`0x03`) and Water Temple (`0x05`) visits depend on the entry room, with no
fallback changes on room transitions. A scene change, native PlayState owner
change, selected scene-resource/setup change or explicit scene reset starts
a new visit. If headers are initially unreadable, the group stays unknown
until a valid sample arrives. An unreadable later sample keeps the latched
group. Unknown scenes retain the prior no-group behavior.

Standards live beside scene files:
`group-<id>.environment.json` and `group-<id>.night.environment.json`.
Their schema is still `"triaevum.scene-environment"`, with `"version": 6`
and `"groupId": "outdoor-time-at-entry"` (for example), instead of `sceneId`.
Exactly one identity is allowed. Lighting fields and ranges match version 5;
night uses `"timeOfDay": "night"`. Old scene files remain valid and are not
rewritten by loading. The tool checks filename identity and time of day for
both canonical filename families; format writes group version 6 or scene
version 5 as appropriate.

Resolution is independent for each slot:

- **Day:** scene day file -> group day file -> `BaseSceneEnvironmentProfile()`.
- **Night:** scene night file -> group night file -> the resolved day result.

**A scene with its own day file now inherits the group's night standard when
one exists.** The existing native blend runs between these two resolved
endpoints. Fixed-time and other non-clock-driven scenes use day only.
Invalid/unreadable files report an error and keep the last accepted value;
they do not silently fall through to a lower-priority file. On initial entry,
an invalid day stays disabled and an invalid night leaves day in use.

F1 shows the group, both sources and the selected source filename. **Save
as group standard** confirms replacing that group's current slot. It takes
effect immediately for the current scene if it inherits the slot; every other
scene inheriting it loads the same standard on entry. Scene-owned files retain
precedence. **Use group standard** confirms deleting this scene's file for
the current tab and discarding its custom draft, then resolves the fallback.
Ordinary **Save day/night/profile** always creates or updates the scene file,
even when editing an inherited standard. A standard save commits an inherited
draft; a draft over a scene-owned file still needs ordinary Save to commit
that scene's values. Unknown groups disable both group actions. Errors are
visible and confirmations become stale if the scene or editor revision changes.

### Native time-speed evidence (EUR revision 0)

Scene-command dispatch literal **0x00379174 -> 0x0053CC84** identifies the
table; command `0x10` entry **0x0053CCC4 -> 0x00217A5C** selects the time
handler. It consumes bytes `[4]=hour`, `[5]=minute`, `[6]=speed`. Only when
both time bytes are not 0xFF does it set the clock from hour/minute. It maps
speed 0xFF to zero and writes **PlayState + 0x3192** (environment +2).
When Sun's Song state at **SaveContext + 0x15B0** is zero, it copies that
speed to the shared field **0x00531EBE** (literal **0x00217C04**).

`Environment_Update` **0x0045DD50** reads that speed through literal
**0x0045E178 -> 0x00531EB4**, offset `+0xA`. Its pause/dialog/cutscene and
frame-advance gates govern advancement of **SaveContext +0xC = 0x00587964**;
the increment also includes the native debug scalar and night multiplier.
With normal debug settings, zero speed holds time; Sun's Song can change it
separately. The effective lighting clock remains **0x00588F00**, as described
below. This classification does not approximate time flow by watching frames.

Selected setup is **0x00588E40**; scene commands are **PlayState +0x110**,
current room number is signed byte **+0x4C30**, current room commands **+0x4FFC**.
The adapter does not substitute the previous room while the current one loads.
Alternate-header handler **0x00378FF8** supports the existing null setup-3
fallback to setup 2. Byte fixtures cover these reads and the shared parser.

### Offline group inventory

```text
triaevum_environment_tool list-groups <romfs-directory-or-Level3/IVFC-image>
```

This reads files only and emits `triaevum.environment-groups` version 1 JSON:
one row per scene stem, standard/MQ variant and selected setup, with each room's
group, hour, minute and speed. `firstRoomGroup` means the lowest numbered room
in the inventory, not a prediction of every gameplay entrance. `mixed` marks
multiple room classes; unknown classes are null. Scene IDs are joined from
asset-name metadata solely for reporting; classification uses headers only.

The EUR inventory has 102 standard scenes at setup 0: 68 first-room indoor,
9 time-flows, 23 time-at-entry and 2 fixed-time. The indoor count includes the
two mixed temples. Ganon's Tower (`0x64`) is also time-at-entry. The full
round-34 coordination report lists each ID; the JSON includes MQ and alternate
setups as well. No extracted file, game configuration or guest state is changed.

## Day/night editing and playback

F1 **Lighting > Lights & Shadows > Scene lights** has explicit **Day** and
**Night** tabs for clock-driven scenes. Either draft can be edited at any
native time. The preview selector is separate: **Auto**, **Force day**, or
**Force night**. The `Applied: day N% / night M%` indicator reports the applied
profile weight. Preview is not serialized. Closing F1 or pressing F2 restores
Auto. Time changes keep both unsaved drafts; leaving the scene reloads files.
Each tab has its own **Save day/night** and **Revert day/night** controls.

**Create night (from day)** creates the night file from the current day draft.
**Copy day -> night** confirms replacement of the night draft; **Save night**
then writes it. **Remove night** confirms deleting the file and discarding its
night draft; the day draft is kept. Removal resolves group night, then day.
Without either night file, day applies at all times. Fixed-lighting scenes show one profile, **Save profile** and
**Revert profile**, with no day/night or preview controls. Existing night files
for those scenes are left untouched and unused.

The scene filenames and schema versions 1 through 5 remain compatible. Loading does not
rewrite files. `SceneEnvironmentRuntime` owns both drafts, selection, preview,
weight and publication; the panel contains no second profile store.

## Native transition curve (EUR revision 0)

`Environment_Update` at **0x0045DD50** selects one of nine records in the table
at **0x00531EFC**, stride **0x36** per weather mode. Each six-byte record is
`u16 start, end; u8 from, to`. Its interpolation is
`1 - (end - clock) / (end - start)`, clamped by the native endpoint selection.
All five modes use the same phase intervals; only light-setting indices differ.
Setting phase `index & 3 == 3` is night. Morning, day and evening belong to the
day profile, so projecting that native interpolation onto night membership is:

| Effective native clock | Night weight |
| --- | --- |
| 0x0000 ? 0x2AAC | 1 |
| 0x2AAC ? 0x4000 | `(0x4000 - clock) / 5460` |
| 0x4000 ? 0xC001 | 0 |
| 0xC001 ? 0xD556 | `1 - (0xD556 - clock) / 5461` |
| 0xD556 ? 0xFFFF | 1 |

Read the effective light-setting clock at **0x00588E58 + 0xA8 = 0x00588F00**,
which the native update can hold relative to `gSaveContext + 0xC` at
`0x00587964`. SaveContext's binary night flag at **0x00587968** is diagnostic
only. Environment context is **PlayState + 0x3190**: light mode **+0x20** must
be zero and setting override **+0xA7** must be 0xFF for clock-driven lighting.
The weather mode blend fields **+0x21/+0x22/+0x23/+0x24/+0x26** do not alter
this weight because both weather schedules have identical night membership.
The fixed-setting blend at **+0xC8** is not the dawn/dusk weight.

Evidence: the native decompilation's lookup and component interpolation at
`0045DD50` and its literal pointers `0045E148=00588E58`,
`0045E160=00587958`, `0045E178=00531EB4`, `0045E188=00531EFC`;
the decoded table is also in
[`oot3d_pica_lighting_semantics.json`](../tools/oot3d/native_demo_host/lighting_tables/oot3d_pica_lighting_semantics.json),
`runtime_environment_light_settings_consumer.transition_modes`. Round 32
checked these records directly against EUR code.bin. There is no fitted
transition duration, binary-flag easing, or wall-clock animation.

## Blending and continuity

Scalars and already-linear profile RGB interpolate directly. Azimuths take
the shortest wrapped route; elevation interpolates between its bounded angles.
This applies to the key/fill, key offsets, ambient/native gains/fog, point-light
groups and rim colour/intensity/sharpness/coverage/azimuth. Placed lights match
by list index; position, rotation, dimensions, reach and beam parameters blend.
Missing or invisible placed lights fade to/from zero intensity. Rim and point
group enable flags also become intensity fades where they control emission.
Top-level enabled/preservation, key-source mode, caster faces, rim source/scope
and placed-light type/shape/shadow/two-sided policies switch at weight 0.5.

Every publication is derived directly from the captured native clock. Scene
changes, clock jumps and savestate reloads use their new weight immediately;
there is no trailing host animation. Day/night share a scene shadow scope and
index-matched placed-light identities. A runtime continuity epoch changes on
edits, scene/load/mode changes and clock cuts above the shadow scheduler's
existing 8-degree threshold. Within an epoch the renderer preserves TAA and
lets `VirtualShadowKeyframes` construct/dissolve moving-direction layouts
under its existing page budget. Caster/enabled/key-source discrete changes
still invalidate history. No per-frame direction equality invalidation is
used for these continuous samples.

A 1,366-frame dusk blend of authored key azimuth 170 to -170 measured one
initial history reset/cut, 69 constructions, 68 rotations and peak construction
budget 16 pages (existing cap 64). This is a CPU execution of the production
scheduler and backend continuity predicate, not a live GPU timing result.

## Deterministic captures

Set `TRIAEVUM_ENVIRONMENT_NIGHT_WEIGHT=0.5` before launch for a fixed half blend.
Only finite values in [0,1] are accepted. It overrides Auto for clock-driven
scenes with a night profile; it neither edits files nor changes guest time.
Explicit F1 Force day/night takes precedence. The indicator identifies the
lab override; returning to Auto resumes it until the environment variable is
removed for the next process. Fixed scenes and scenes without night ignore it.

Developer stderr `TRIAEVUM_ENVIRONMENT_SELECT` now includes `clock`,
`timeDriven`, `nativeNight`, `nativeWeight`, applied `weight`, `preview`
(0 Auto / 1 day / 2 night), `forced`, `nightProfile`, editor tab, both dirty
flags and continuity `epoch`. Round 34 adds `group` and `daySource`/`nightSource`
(0 scene, 1 group, 2 Base, 3 day result). It reports changed publications plus a heartbeat
every 120 observations. For live cache checks, inspect the existing
`nri_vsm_keyframe_*` and `nri_vsm_pending_pages` diagnostics: ordinary blending
should advance construction/dissolve without repeated cuts.

## triaevum_environment_tool

Developer builds (`TRIAEVUM_DEVELOPER_TOOLS`), target
`triaevum_environment_tool` (not built by default). It never starts the game.

```text
triaevum_environment_tool validate <file>...
triaevum_environment_tool format [--check] <file>...
triaevum_environment_tool explain <file>
triaevum_environment_tool list-groups <romfs-directory-or-image>
```

- `validate` prints one JSON line per file:
  `{"file", "valid", "sceneId" or "groupId", "timeOfDay", "errors": [{"pointer", "message"}]}`.
  Every value is checked and every problem reported at its JSON pointer
  (`/placedLights/1/intensity: is 30; must be a number within 0..16`); a
  file named `scene-XXXX[.night].environment.json` must name that scene and
  time of day. Canonical group filenames must match group ID and time of day.
  Files are read as the runtime reads them (64 KiB at most).
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
  device decide which of these apply, units and angle conventions. Group
  explanations show identities using scene 0 and explicitly note that runtime
  identities are scoped to each consuming scene.

Exit status: 0 success, 1 an invalid or non-canonical file, 2 usage.

## Hot reload

`TRIAEVUM_ENVIRONMENT_HOT_RELOAD=1` makes the running game re-read the current
scene's own files or the group files it inherits when they change on disk.
Only developer builds contain the watcher. It looks at both resolved slots
(file source, size and time) at
most every 250 ms and acts on a change once two looks agree, so a file still
being written is not read; a file rewritten with the same content is no
change. Each outcome is a stderr line:

```text
TRIAEVUM_ENVIRONMENT_RELOAD scene=0x0055 time=day result=applied file=scene-0055.environment.json
```

- `applied`: the file validated and is now the profile in use.
- `kept-invalid`: it did not validate (`error=` gives the first error); the
  profile in use stays, so a half-written file never reaches the renderer.
- `kept-missing`: legacy unknown-group day file removal keeps the profile in use.
  For known groups, removal resolves the next fallback and reports `applied`.
- `kept-unsaved`: that tab has unsaved edits; they are never replaced. The
  change waits for Save or Revert in that tab. The other tab can still reload.

Both drafts are watched independently in clock-driven scenes. A valid night
file enables blending; its removal resolves group night, or disables blending
and returns the editor to Day when neither night file exists, unless its dirty
draft must be preserved. An invalid new night file
leaves the day profile in use and cannot be overwritten by Create night.
Fixed scenes watch only day. The game's own saves acknowledge only their own
file. Scene/tab changes and active-profile reloads advance the editor revision
so stale edits cannot overwrite another selection. Native time changes do not
advance it or discard either draft.

## Identity

The codec's standalone `explain` identifies authored placed lights by tag,
time of day, scene and full 32-bit id (`PlacedLightIdentity`). Runtime blending
uses one scene scope and stable list-index identities across both endpoints;
a change to profile topology or a manual edit cuts history. Authored ids and
files are not rewritten. A scene with no night profile keeps authored ids.

## Verification

Round 34: `TriAevum.exe`, `triaevum_environment_tool` and all touched test
targets build. `triaevum_environment_tests` passes 655,553 checks; the native
room/header suite passes (all four groups, FF speed, fixed/partial-FF time,
unknown headers, alternate setup, capture offsets and RomFS directory/image
equivalence). The focused actual-widget F1 smoke passes 2,660 assertions.
32 CLI invocations cover all eight canonical group filenames, format/check,
explain, wrong identity/time/version rejection, unchanged scene version 5,
the complete EUR inventory and missing-input failure. The full F1 suite still
stops at its previously documented unrelated material-editor Azahar-identity
assertion. No game, git command or play-configuration edit was performed.

Round 32: `triaevum_environment_tests` passes 655,513 checks, including every
clock value in all five audited native schedules, adapter offsets/failures,
linear RGB and wrapped angles, discrete/fade policies, independent drafts,
hot reload, forced preview, scene/load/time snapping, invalid-file protection
and the production shadow-scheduler simulation. The focused real-widget
`triaevum_f1_settings_smoke --scene-environment` passes 2,364 assertions.
`TriAevum.exe` builds. The full F1 suite still stops at the previously recorded
material-editor Azahar-identity assertion; no game was run for this round.

Earlier tooling validation (before the round-32 UI and runtime changes):

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
