# Issue 48: Start During Title Presentation

## Reproduction

The published Windows alpha.3 runtime consistently exits with status 1 when
running `input_timelines/title_to_file_select.json` from a clean boot with the
Forge-created default TopScreen/interpolated-60 profile. The renderer reports:

`native PICA composition layer contradicts its domain`

The release qualification covered unattended title playback, but did not press
Start to enter file selection. That coverage gap allowed this regression through.

## Cause and Correction

`oot3d_native_a32_window.cpp` deliberately uses an Unknown command-list domain
outside gameplay. Native command spans can nevertheless identify individual
draws as OpaqueWorld, TransparentWorld or Atmosphere, including title/menu draws.
The PICA frontend previously derived a domain from a typed span only for UI;
known scene layers inherited Unknown and violated the scheduler's invariant.

`oot3d_native_pica_frontend.cpp` now derives Scene for all three known scene
layers, Ui for UI, and retains the list hint only for Unknown layers. The
existing native UI-lifecycle override remains authoritative. No scheduler
validation is removed, no draw order changes, and no title/asset-specific
exception, guest/AOT change, or optional-effect workaround is introduced.

## Verification

- PICA frontend unit suite passes, including a new matrix covering the three
  scene layers with an Unknown list hint, adjacent unclassified/UI spans, and
  preservation of whole-list native UI ownership.
- Vulkan bridge tests pass.
- The original failing early-Start timeline passes 1,800 presented frames,
  exits zero, and reaches file selection in the framebuffer capture.
  There are zero composition publication failures and zero pending draws.
- `input_timelines/title_start_late_file_select.json` provides a second
  repeatable path: Start at frames 1,500 and 1,700. The 2,400-frame test also
  exits zero and reaches file selection, using the published Windows package
  dependencies with only its runtime executable replaced.
- Private logs, framebuffer captures and runtime summaries are under
  `I:/TriAevum-public/issue48-verification`; the failing release log remains in
  the alpha.3 qualification directory. No private evidence is committed.

The shared correction is platform-independent, but these live runs qualify
Windows only. Existing published alpha.3 assets are not silently replaced;
the fix requires a subsequent release or a build containing this change.
