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

The initial live runs above qualify Windows. Subsequent alpha.3b qualification
also passes on Linux/Wayland; see the release record below. Published alpha.3
assets are not silently replaced: alpha.3b carries the correction.

## Alpha.3b Windows/Linux Qualification

Runtime/source freeze: `6863881315843991d2ffe754831b75281056f2ce`.
Linux was rebuilt in the pinned Steam Runtime SDK from the matching exported
source, with no platform-specific workaround. Frontend and Vulkan bridge tests
pass on both platforms. Forge, title modules, corresponding translated sources,
shader corpus and other unchanged alpha.3 dependencies retain their identities.

Both new packages pass the allowlist/source/hash audit: Windows 65 files,
Linux 531 files. Frozen Forge successfully updates private alpha.3 installations,
preserves user configuration and reuses imported data/cache; no title compilation.
Renderer preparation reports all 22 embedded artifacts available.

| Final packaged runtime | Early Start | Late Start |
| --- | --- | --- |
| Windows | 1,800 presentations, 38,530 guest draws | 2,400 presentations, 75,802 guest draws |
| Linux/Wayland | 1,800 presentations, 38,796 guest draws | 2,400 presentations, 77,015 guest draws |

All four runs exit zero, produce framebuffer captures showing file selection,
and report zero pending draws and zero composition publication failures. These
are functional checks, not frame-rate or cross-platform pixel parity claims.
The final mounted AppImage also passes a 900-presentation early-Start run through
its normal AppRun/Forge launcher. Only the private test profile and its matching
receipt were temporarily bounded for capture and restored afterwards.

Final asset SHA-256:

- Windows ZIP: `686082b1bf1a2d519a2380065f40dce4fb88fee404661a8113f7032727d55be1`.
- Linux AppImage: `f1d1917c9dd1048b3cf045b730fa1689af6110918d3ed65fc0ba3aebe3f6ace1`.

Private evidence is under the respective `releases/v0.6.0-alpha.3b` (Windows)
and `/home/xander/triaevum-alpha3b-20260918` (Linux) directories. Steam Deck
hardware is unavailable; no new hardware certification is claimed.
