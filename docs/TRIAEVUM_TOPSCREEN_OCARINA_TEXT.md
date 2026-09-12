# TopScreen 2.1.1: Native Localized Song Text

Date: 2026-09-10. Extends the song guide introduced by `706a046`;
see [owner and PR attribution](TRIAEVUM_TOPSCREEN_OCARINA_OWNER.md).

## Delivered

The selected learned song now has its localized name above the staff, with
the native font, native drop shadow and TopScreen's song color. Changing songs
updates the name; leaving the ocarina removes it. Unknown songs retain the
existing question marker rather than revealing their name.

The original game's message builder produces the actual glyphs and their
layout. There is no host-font replacement, hardcoded English song-name list,
lower-screen framebuffer promotion or scene-renderer change. This is a source
reimplementation of the relevant policy from rlgcarrot's TopScreen mod, not
execution of its injected payload. PR #18's evidence and author are credited
in the preceding report; this module is not a wholesale PR merge.

## Owners And Evidence

Read-only evidence: official TopScreen 2.1.1 EUR exports under
`I:/oot3dre_work/topscreen-2.1.1-reference/ghidra_export/decompiled/`, and
existing native exports under `I:/oot3decomp/ghidra_export/decompiled/`.
No new decompilation or external-repository changes were needed.

| Entry/data | Responsibility |
| --- | --- |
| Mod `0x005D4A8C` | Selected-song message, allocation, constructor arguments and foreground color |
| Native `0x004D5480` | Twelve message indices; selected index plus `0x9AD` |
| Native `0x00313CE0`, `0x003525D4` | Allocate/free the `0x4C`-byte overlay owner |
| Native `0x002F57F0` | Construct localized text with arguments `(owner, message, 0x20, 0x1E, 0)` |
| Native `0x003446E8` | Localized message/font generation; language owner from literal `0x003448C8`, field `+0xF3C` |
| Native `0x00348BE4`, `0x00348F34` | Packed model buffer allocation and model construction |
| Native `0x002F6944` | Destruction, including original GPU completion waits |
| Mod `0x005CAD58` | Destruction/deallocation order |
| Mod `0x005D2078` | Text draw and rotated viewport offset `-40`, equivalent to host X `+40` |

The native overlay supplies two models/textures: shadow first, foreground
second. Descriptor flags determine packed position/normal/UV/color offsets.
The actual text generator emits **TL, BL, TR, BR**, unlike ordinary UI quads.
This order is covered by fixtures and was checked against live vertex data.

Generated CTXB metadata supplies extent, format, allocation size and surface.
Its A8/A4 atlas is coverage: native text uses primary RGB and sampled alpha.
For the host UI's RGBA multiplication, the title-side texture provider emits
white RGB with unchanged coverage. Canonical PICA A8/A4 decoding is untouched.

## Module And Lifetime

- `tools/oot3d/native_game_runtime/oot3d_top_screen_ocarina_text_runtime.{h,cpp}`:
  resumable native lifecycle and immutable host texture snapshot.
- `tools/oot3d/ui_topscreen/oot3d_top_screen_ocarina.{h,cpp}`:
  message selection and validated glyph-stream interpretation.
- `tools/oot3d/native_game_runtime/oot3d_native_ui_texture_provider.cpp`:
  generated CTXB validation/decoding and alpha-mask lowering.
- `oot3d_native_a32_window.cpp`: explicit native-dispatch registration,
  UI composition, quickload reset, pending-save deferral and diagnostics.

Only message/language changes schedule work. The native input-update boundary
`0x0041E988` dispatches allocation, construction, capture, destruction and free
as resumable native calls. Caller registers, flags and VFP state are restored
before its original continuation. Extra returns must not advance the input
cadence a second time. Synchronous nested calls are deliberately not used:
the destructor's original GPU wait needs the normal process scheduler.

Both glyph layers and decoded textures are captured atomically before freeing
the native resources. Presentation identities contain content-derived keys,
not freed guest pointers. Repeated frames do not rebuild localized text.
Failed capture still runs native destruction/free and publishes no partial
snapshot. Structural failures are reported rather than guessed around.

Quickload clears the host cache and pending state. Saving is deferred while
the injected native continuation is pending, so a standalone savestate cannot
contain a return continuation whose host state was not serialized. No guest
savedata fields or savestate format were added.

## Verification

Build/test commands on the Linux host:

```sh
cmake --build /home/xander/triaevum-linux-build --parallel 3 --target \
  oot3d_native_game oot3d_top_screen_ocarina_text_runtime_tests \
  oot3d_top_screen_mod_profile_tests oot3d_native_ui_texture_provider_tests
/home/xander/triaevum-linux-build/oot3d_top_screen_ocarina_text_runtime_tests
/home/xander/triaevum-linux-build/oot3d_top_screen_mod_profile_tests
/home/xander/triaevum-linux-build/oot3d_native_ui_texture_provider_tests
```

All three test executables pass. Coverage includes native call ABI, stack
argument, state restoration, deferred cleanup on invalid models/textures,
atomic publication, unchanged-message reuse, language invalidation, reset,
unlearned-song suppression, descriptor bounds, UV order and alpha coverage.

Live qualification uses Linux NRI/Vulkan/Wayland at 1280x720, native 30 Hz
without interpolation or extension effects. Runs use isolated copies of
original saves and bounded capture through `tools/triaevum_release/probe_renderer.py`.
Private evidence is under `/home/xander/triaevum-pipeline-live-proof/`:

- `ocarina-text-qualified`: 400 presentations, three text builds and three
  native releases, no owner-read errors. Frame 240 shows the pink **Zelda's
  Lullaby**, frame 270 the yellow **Sun's Song**; frame 390 shows ordinary
  gameplay/HUD after B with neither text nor guide remaining.
- `ocarina-text-regression-even` / `ocarina-text-regression-odd`: twelve
  inventory captures match `ocarina-regression-even` / `ocarina-regression-odd`
  byte-for-byte, covering both phases of native input refresh.
- `ocarina-text-regression-title`: 900 presentations; all six captures match
  `ocarina-regression-title` byte-for-byte. Together these regressions verify
  eighteen unchanged framebuffer captures outside the ocarina text surface.
- `ocarina-text-final`: final atomic-snapshot build, same ten ocarina captures
  byte-for-byte, plus a successful savestate at run frame 240 while the title
  is visible. `ocarina-text-reload-browser` loads that checkpoint in a fresh
  process, browses three selections and closes with B: 300 presentations,
  three builds, three releases, zero owner-read failures, regenerated native
  text confirmed from the framebuffer. The host browser intentionally resets
  on quickload; it is not serialized into the guest save.

The first reload probe reused the earlier note-input sequence and completed a
native song instead of testing browsing. Its successful run is retained as
`ocarina-text-reload`, but is not counted as text-regeneration qualification.
The dedicated browser-only timeline removes that ambiguity.

Each probe directory contains exact `invocation.json`, runtime diagnostics
and framebuffer captures; assets, saves and captures are not published.
This is functional qualification, not a performance benchmark.

## Remaining Work

Updated qualification and subsequent fixes are recorded in
[ocarina and mapped actions](TRIAEVUM_TOPSCREEN_OCARINA_AND_ACTIONS.md).
In particular, the guide toggle has since been implemented and the native
recognition staff observed in real gameplay; do not reopen those as absent.

Free-play instructions, performed-note feedback, recognition/learning
transitions and the original guide-mode toggle are still separate owners.
The native ocarina draw `0x00425930` and globals `0x005093E4..0x005093F4`
remain the starting evidence for these paths; do not substitute the song's
reference sequence for actual performed notes.

The live fixture is English and fully learned. Other languages and an
unlearned selection have code-level tests, not equivalent live captures.
Windows/Android binaries were not rebuilt or visually qualified in this
tranche. No new Azahar capture or full completed-song comparison is claimed.
