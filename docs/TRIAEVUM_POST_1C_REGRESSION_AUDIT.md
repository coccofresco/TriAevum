# Post-1c Regression Audit

September 11, 2026. Baseline `v0.6.0-alpha.1c`, examined snapshot `5cff35e`:
**87 commits**, inventoried chronologically in the
[commit ledger](TRIAEVUM_POST_1C_COMMIT_LEDGER.md).

## Conclusion

The post-1c changes remain in the shared source; the exercised tests and game
runs pass. **This is not a certificate that every feature is deployed or that
all gameplay is regression-free.** The audit found a real Linux title deployment
gap, a broken published F1 test command, and outstanding distribution work.
An updated renderer alone was insufficient evidence of a fully updated game.

## Findings and Corrections

### Linux title missed the VFP64 fix

`c7f9686` repairs double-width VFP memory operations used by native gyro code.
The host Linux title had this correction, but the Steam-SDK title retained
SHA `ac927143b9cf98b99597eb52b6214c0358f8ea755757a0941641f48f289dca47`.
That older title was still selected by the installed Flatpak. The immediately
preceding Grass alignment did not inspect this distinction; its renderer tests
do not establish preservation of the aiming fix.

Rebuilt the SDK title against the existing hash-verified corrected source
inventory: six shard compilations and link, not 257 objects or another
translation. New title SHA:
`97de67f0e595375761661831fa65c4e14f9a70eee52849a4a55d89459d79be15`.
Source manifest SHA:
`2328fd61f17120d1f3f38bde7ba9431e87453115b2b241ed4c4f7c1d36ae0f84`.

Updated the private package title catalog, its corresponding translated-source
archive, and the installed immutable plugin generation/profile/receipt. Old
files were backed up; runtime/plugin/profile identity validation remains on.
The legacy TAM was not rewritten: this launch path loads the selected title
plugin directly. Do not represent that as regeneration of every old receipt.
Flatpak commit `1e76d83738f44879815e4bd2cdf78cb4740545029ce26f83d4e4d227fdb86d80`
is installed. A native gameplay checkpoint replay and a normal Forge-to-game
launch both exit 0 with the corrected title. The generated VFP execution suite
passes on both systems; physical gyro ergonomics were not retested.

### Published Windows F1 test runner had become incomplete

The widget fixture gained language tests in `ac77f6c`, but
`run_f1_settings_smoke.ps1` did not link the two language objects. Running the
documented command failed at link time despite earlier private successful
fixtures. Added those objects and reran successfully: **3,896 assertions**.
Added a public Bash counterpart using the same fixture/runtime objects. It
also passes 3,896 assertions, without depending on an untracked Linux CMake
fixture. This repairs verification infrastructure, not a missing game panel.

### Distribution is still not completely at parity

- The private Flatpak still contains its older frozen Forge. The current Forge
  source tests pass, including the real language widget, but this does not
  activate those features in that older executable. The SDK currently lacks
  Python Tk; a proper portable Forge build remains required.
- The Flatpak catalog still lacks the current `renderer_shader_preparation`
  helper contract. Its 200-module baseline pack plus persistent runtime cache
  is not equivalent to current Windows Forge prewarming. Packaging the helper
  and its dependency closure remains necessary.
- SSSR is disabled in the Linux configuration. This is a known platform gap,
  not a newly deleted source feature. It cannot be advertised as Linux parity.
- Automatic migration of an existing Flatpak activation receipt after a package
  update remains incomplete. The two private upgrades were explicit verified
  migrations, not a general product fix. Preserve this release blocker.

## Feature Retention Matrix

"Tests" means current executable/unit/widget execution, not an all-game visual
claim. Historical in-game evidence is linked separately and not recounted as
a new test. Domain overlap is intentional.

| Surface / representative commits | Current evidence | Remaining boundary |
| --- | --- | --- |
| Linux/portable packaging, UI headers: c5d8534, f1c05a8, bde0f02, 8cbba28 | Both native builds; release, catalog, storage, portal, migration tests | Frozen Forge/receipt migration above; no new public release |
| Escape, Wayland, swapchain: 170382f, 76d7e44, 777d39a | Recent native/x2 Flatpak captures and current normal launch | Not every compositor/device; no physical Steam Deck |
| Shader seed, SPIR-V and NRI preparation: 42e0948 through fa86170 | Real cold/warm/corruption tests; Windows relocated helper; actual cache hits | Incomplete game corpus and Linux helper distribution |
| Save deletion and loader: d1f57f9 | Current host/filesystem/native-process tests on both OSes | Full native erase interaction not repeated in this audit |
| VFP/gyro: c7f9686 | 33 generator/execution tests on each OS; SDK title corrected | Physical controllers and every aiming context not repeated |
| Controls, recapture, binding capture: d941550, 79d05c1, 23e44c9 | Current native input tests and real widgets on both | Hardware-specific mouse/controller ergonomics |
| Display/fullscreen/aspect: 421ec8e | Current Windows NRI live fractional/4:3/4K/borderless test, exit 0; both widget fixtures | Linux live resize evidence from prior alignment, not repeated here |
| Language: ac77f6c | Built-runtime language detection on both; Windows and Linux Tk/widget tests | Older frozen Linux Forge still needs replacement |
| TopScreen items, songs, visibility and mounted layout: 7617c8a through 0079a43 | Both current profile/input tests; Linux partial-progress game replay; Windows HUD capture | Not all mounted/minigame contexts visually requalified |
| Grass terrain color/light, adaptive clusters: 7005c3f through d3ffc1f | Current defaults, cluster tests, Windows/Flatpak gameplay; recent paired Grass evidence | No claim that all scenes or GPUs meet the measured performance target |
| PR attribution and rejected guest worker: 5c6d93f, dd7539e, 09f6202, fa86170 | Contribution record and source retained; PR15 worker remains intentionally unimported | Rejected work is not a regression or missing accepted feature |
| Android-specific commits | Shared source retained and included in history | Android device execution outside this desktop audit |

No tracked files were deleted between the tag and examined snapshot. This fact
alone does not prove semantic retention; the tests and artifact checks above
are the substantive evidence.

## Current Runs

- Windows release-tool discovery: 353 tests, 10 skips, no failures.
- Linux discovery with isolated Tcl/Tk libraries and actual display: 353 tests,
  13 skips, no failures. Initial failures were missing test-host Tk and a home
  directory module shadowing discovery; neither was hidden as a passing test.
- Built language/shader tests: Windows 12 pass; Linux 9 pass. Windows relocated
  preparation compiles 22 modules cold, then hits all 22 with zero compilations.
- AOT generation/VFP execution: 33 tests pass on each OS, including emitted
  machine-operation behavior rather than source-string checks alone.
- Native input, CTR host, TopScreen profile and filesystem test executables
  pass on both systems. F1 widgets: 3,896 assertions per OS.
- Current Windows display/gameplay probe: 360 presentations, x2, user effects,
  fractional scales 1.01/0.73, 4:3/720p/4K, fullscreen confirmation and return.
  Exit 0; no error/VUID entries in the log. Actual cache counters: pass 23/23,
  SPIR-V 64/64, zero compiles. Direct framebuffer inspected; HUD/world present.
- Corrected SDK title in Flatpak: 540-frame partial-progress/ocarina replay,
  exit 0, native framebuffer inspected. Pass cache 20/20; 26 additional PICA
  modules compiled and persisted. This explicitly is not zero-compilation.
- Normal installed Forge launch with corrected title: bounded 35-second run,
  exit 0, report and captures. No test processes are intentionally left open.

Counts overlap between discovery and explicitly enabled integration subsets;
do not sum them into a count of distinct checks or a gameplay coverage percent.
The recent intro/Grass results remain in
[Linux alignment](TRIAEVUM_LINUX_GRASS_ALIGNMENT.md) and
[Grass acceptance](TRIAEVUM_GRASS_GOAL_ACCEPTANCE.md); not all were rerun here.

## Remaining Regression Work

Update: portable Forge and its renderer shader helper are now built and staged
in the installed private Flatpak. Shared automatic package-update handling is
implemented with 26 targeted tests per OS and an actual Windows reactivation.
See [package parity](TRIAEVUM_DESKTOP_PACKAGE_PARITY.md) for exact artifacts,
counts and remaining packaged/visual qualification. Do not treat the old Forge
distribution gap as unchanged, or the remaining GUI/SSSR work as closed.

The previously documented outline rectangle in letterboxed weapon aiming has
no subsequent corrective commit identified in this range. Keep it open; do not
hide it by disabling outline defaults. See the evidence in
[issue qualification](TRIAEVUM_ISSUES_5_12_17_19_20.md).

Next qualification should first close portable Forge/helper distribution and
transactional package-update activation, then replay mounted HUD, item opacity,
mouse/gyro aiming and first-time learning with the *packaged* artifacts. Bind
each package to runtime hash, title source manifest and title binary hash; an
unchanged ABI or a successful intro is insufficient. Preserve native/x2 timing
separation and isolate all saves/configs used by probes.

Private evidence: Windows `J:/TriAevum-verify-20260910/display-x2-221117/` and
the current runtime/widget outputs; Linux `~/triaevum-post-1c-audit-backup/`,
`~/triaevum-align-d3ffc1f-backup/release-audit-tests.*`, and Flatpak probes
`aligned-d3ffc1f-audit-gameplay` / `post-1c-launcher-audit`. No private assets,
screenshots, SDKs or caches are committed or uploaded.
