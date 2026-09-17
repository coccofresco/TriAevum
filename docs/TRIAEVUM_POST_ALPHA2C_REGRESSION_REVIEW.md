# Post-alpha.2c Regression and Linux Alignment

Date: 2026-09-17. Audited source: `e84ad86`, plus fixes recorded below.
Baseline: `v0.6.0-alpha.2c`. Scope is the 81 non-merge commits below;
PR assessment/integration is not repeated. This is not an all-game certification.

## Acceptance Matrix

| Area | Regression contract | Verification |
| --- | --- | --- |
| Renderer | TEV operand precedence; lighting, fog, alpha, textures, procedural textures; native/temporal/offline families remain equivalent | Rebuild and run standalone TEV/PICA CPU and differential GPU suite on Windows and Linux |
| Pipeline lifetime | Fixed-state normalization preserves rendering; GPL stays optional; retained pipelines and fences have bounded lifetimes | Pipeline identity/library tests; live native and advanced runs |
| Toon/background | Raster initialization is not toon-lit; outline eligibility remains native; no HUD as scene input | Shader pipeline tests; framebuffer boot and user Sages checkpoint |
| Grass | Spacing, partitioning, ordering and visibility retain coverage | Standalone grass/cache tests; advanced Kokiri and title runs |
| Pacing | Native 30 Hz gameplay, genuine x2/x3 samples, VSync/limiter honored; no benchmark clock in interactive launch | Clock/pacer tests; runtime timing diagnostics; no fixed delta or input timeline in interactive use |
| UI | Centered original aspect; minimap placement; Visions ownership and movie decode; race counter; independent boots/actions | Composition/canvas/TopScreen/lifecycle/Y2R tests and private runtime fixtures |
| Menus | Consolidated F1 only; deferred retained F1/F12 branch not active; persistence and display confirmation | Actual-widget smoke; settings/persistence tests |
| Input | Stable selected device, touch, auto aiming, simultaneous gyro+stick, mouse ownership, sensors never freecam | Shared input/native input/SDL virtual devices; actual-widget tests; hardware limits stated separately |
| AOT | Rejected experiments remain opt-in; baseline title still selected; recovered movie functions retained | Source/build configuration audit, plugin identity, live boot/gameplay/movie |
| Package | AppImage uses persistent XDG activation; no end-user compilation; notices/allowlist retained | Forge/release Python tests; portable SDK build and product-info |
| Maintenance | Cleanup/history/docs do not remove runtime prerequisites | Full source mirror comparison; incremental builds, launch dependency verification |

## Initial Findings

- Linux mirror had 283 missing/different files out of 3,575 tracked source files.
  Its Git HEAD was stale and did not identify its compiled source. Full backup:
  `~/triaevum-before-parity-20260917.tar.gz`. Update copied only differing
  tracked files, preserving unrelated files, dependencies and build objects.
- The Linux controller test target was absent: SDL's imported target existed
  only in the renderer directory scope. Root CMake now discovers SDL for desktop
  Linux/macOS test registration, using the same implementation as Windows.
- The preceding interactive Windows launch retained a profiling fixed delta.
  Corrected launcher uses resource arguments only, real clock, x2/60 Hz and
  saved graphics settings. Separate 20 s check: 884 presentations / 446 game
  updates, 438 intermediate samples, VSync and limiter on, benchmark off.
- Initial pre-existing Windows test binaries are not accepted as current-source
  proof: several standalone targets were missing; toon artifact check failed;
  graphics persistence test reported an unexpected normalization write.
  Rebuild and diagnose before classifying these as runtime regressions.

## Results

Review completed against the inventory below, with the explicit residual limits
listed here. This qualifies the exercised scenarios, not every gameplay state.

### Corrections

- Restored Linux registration of the shared SDL controller tests (directory-scoped
  imported target); linked Vulkan headers explicitly for the foundation tests.
- Normalized the graphics persistence fixture before counting user saves. Its
  previous unset grass LOD reference caused a legitimate automatic schema write;
  this was a fixture defect, not a runtime persistence failure.
- The raster-initialization exclusion introduced in `ebd6ba6` added a projection
  predicate missing from offline toon enumeration. Enumerated both projection
  states: **348 programs instead of 340**. All 340 previous programs remain
  byte-identical by source identity; eight programs were added. Both platforms
  were rebuilt with this generated library, without scene-specific exceptions.

### Executed Checks

| Check | Windows | Linux |
| --- | --- | --- |
| Actual F1 widgets | 3,978 assertions passed | 3,978 assertions passed |
| Graphics foundation | 291 tests passed | 291 tests passed |
| Shader pipeline | 40 tests passed | 40 tests passed |
| Standalone TEV/PICA CPU/GPU suite | 19/19 passed | 19/19 passed |
| Forge/release Python suite | 396 tests, 14 skips, no failures | 396 tests, 20 skips, no failures |
| Shared/native input, SDL virtual devices, TopScreen, lifecycle | Passed | Passed |
| Presentation clock, including suspended frames | Passed | Passed |
| Real-clock 30/60/90 Hz runtime matrix | 3/3 passed | 3/3 passed |
| Boot, advanced Kokiri, Sages/toon, ranch race captures | 4/4 passed | 4/4 passed |

Additional checks: Windows AOT diagnostic Python suites passed (51 + 17 tests);
Linux native module/service suite passed all 12 executables. Physical controller
probe opened zero devices: no real PS4/PS5/Switch motion qualification is claimed.

Real-clock runs had no fixed delta and no throughput override. Windows recorded
211/210/0, 647/325/322 and 943/329/614 presentations/game updates/intermediate
samples respectively. These confirm interpolation activity, **not performance
benchmark results**: cold startup, advanced effects and concurrent verification
work are not a controlled throughput comparison.

Framebuffer comparison between platforms: boot frame 9, race frame 60 and Sages
frame 60 were pixel-identical. Kokiri frame 60 differed by eight total green-channel
levels across 1280x720 pixels (green MAE 0.00000868; red/blue identical).
Linux Sheikah Stone playback completed 1,501 frames / 102,826 draws with zero
guest faults, including the movie and return to gameplay. The portable Steam
Runtime build separately passed boot, real-clock x2 and advanced Kokiri.

### Linux Delivery State

Both host and Steam Runtime SDK builds are updated, including SSSR support.
SDK binary requires at most GLIBC 2.38. The existing movie-capable title module
was retained; rejected AOT pilots were not activated. The mirror's stale Git HEAD
is not evidence of its contents: tracked sources were compared/copied explicitly,
with the full pre-update backup retained.

The old desktop shortcut still opens the previous Flatpak. A separate desktop
and applications entry, **TriAevum - Updated Development Build**, launches the
tested SDK runtime with the private Linux installation profile. No Flatpak or
AppImage was republished/repackaged. Actual Steam Deck hardware remains untested.
The review launcher selects `--pica-parametric-tev`; the product's default remains
unchanged. Source alignment must not be mistaken for switching that default.

Binary SHA-256:

- Windows: `1596c7295da2a5f819b973c4240af5746d3acbf9a572ca2fc9b4ef780b39ccfd`.
- Linux host: `201d9d9d78d09c7ed7baf4c9863ac5d95e513ddd927e12ecb8ef8bbf8355d5f0`.
- Linux SDK: `247b3b5deadcbce95780481b579bddc3308d6ed729b14ebbc0d1686ef35af7d8`.
- Linux title module: `fbf5291cc268d5162c58162319e625bba56877b5700ef0d26c937535592b4b69`.

### Residual Limits, Not Silently Waived

- Full advanced grass profile still requests two runtime pass compilations on
  cold first use. Native fragment/vertex programs use built-ins; the complete
  effect path is **not** yet entirely runtime-compilation-free.
- Strict specialized-versus-parametric oracle fails bit equality: six pixels at
  Kokiri frame 60 (maximum RGB difference 41/33/25, MAE below 0.00022 per channel).
  The tiny difference persists with grass disabled; its origin is not established
  by this review. Do not call it proven harmless, fixed, or a newly introduced
  regression. With grass disabled the run requests zero runtime shader compiles.
- Virtual input tests do not substitute physical sensors or manual interaction
  coverage. Android/macOS binaries and Steam Deck hardware were not tested here.
- No general new performance improvement is claimed from these correctness runs.

### Reproduction and Evidence

`tools/triaevum_release/tests/review_runtime_matrix.py` reproduces private-fixture
desktop runs. Required arguments: `--executable --profile --config --topscreen
--kokiri --output`; optional `--sages --race --only`. It imports only resource
arguments, copies settings, isolates save data, bounds runs, and records commands,
runtime diagnostics and framebuffer captures. Deterministic capture uses a fixed
clock; timing cases explicitly do not. Never use its capture settings for play.

Private evidence (not distributed):

- Windows: `C:/Users/xander/triaevum-review-timing-20260917/`,
  `triaevum-review-captures-20260917/`, `triaevum-review-linux-images/`;
  strict oracle failures: `triaevum-review-native-parity-20260917/` and
  `triaevum-review-canonical-parity-20260917/` under the same user directory.
- Linux: `~/triaevum-review-timing-20260917/`,
  `~/triaevum-review-captures-20260917/`, `~/triaevum-review-steamrt-20260917/`,
  `~/triaevum-review-movie-20260917/`, `~/triaevum-parity-tev-final.log`.
- Linux source inventory: `~/triaevum-parity-files-20260917.json`; original
  synchronization archive: `~/triaevum-parity-e84ad86.zip`.

## Complete Commit Inventory

Each commit is assigned to an acceptance group above. Reverted menu work and
rejected AOT experiments are checked for non-activation, not reintroduced.

| Commit | Group | Change |
| --- | --- | --- |
| `e419028` | Package | docs: record verified alpha.2c publication |
| `7b88099` | Package | Add audited AppImage staging and persistent Linux activation layout |
| `ddf85a3` | Package | Pin AppImage publisher tools and record Linux candidate qualification |
| `06f5d3c` | Input | Document controller donor assessment and bounded integration priorities |
| `65642a6` | Renderer | Map native OOT3D material consumers and precompiled shader migration |
| `12b2c97` | Renderer | Add data-driven shared PICA TEV core and Vulkan differential test |
| `fd8905e` | Renderer | Fix canonical TEV operand precedence and verify parametric GPU parity |
| `f875e16` | Renderer | Wire opt-in parametric TEV through draw uniforms and visual replay codec |
| `ddd9ce4` | Renderer | Validate opt-in parametric TEV in live NRI framebuffer comparisons |
| `a6ccd1c` | Renderer | Implement parametric PICA lighting, fog and alpha in native NRI path |
| `d61c2f1` | Renderer | Make native PICA texture selection parametric with verified NRI parity |
| `054bb5b` | Renderer | Add data-driven native PICA procedural texture program with GPU parity tests |
| `801a359` | Renderer | Decouple native NRI pipeline identity from material state |
| `8727eb4` | Renderer | Use offline-translated native vertex family in parametric PICA path |
| `247f234` | Renderer | Embed finite native PICA fragment SPIR-V family ahead of shader caches |
| `91d4c5b` | Renderer | renderer: use title-owned precompiled canonical and temporal vertex programs |
| `8a9af30` | Renderer | renderer: precompile fixed NRI passes and compatibility scanout |
| `aadc050` | Renderer | renderer: share effective shader programs across material aliases |
| `74c9eea` | Renderer | benchmark: measure native architecture throughput and record parametric regression |
| `cc04157` | Renderer | fix(renderer): statically lower fixed PICA operand and sampler slots |
| `dba312d` | Renderer | perf(renderer): precompile temporal PICA families and measure frame-time tails |
| `677c5c2` | Renderer | renderer: precompile compatibility combiner family without shader caches |
| `d42143c` | Renderer | renderer: exclude dormant fixed-function state from pipeline identity |
| `fd9195a` | Renderer | renderer: separate NRI pipeline ownership from lazy Vulkan fallback |
| `aaa3530` | Renderer | renderer: share exact normalized NRI pipeline objects |
| `8735f46` | Renderer | renderer: make toon appearance uniform and precompile its base family |
| `4d0e39b` | Renderer | renderer: precompile toon and temporal composition without shader packs |
| `e6490d7` | Renderer | Defer fallback shader modules until Vulkan submission; measure first-use costs |
| `a9d9441` | Renderer | Retain bounded device pipeline objects across logical retirement |
| `55066b0` | Renderer | docs: define impact-ordered structural stuttering resolution strategy |
| `6f04bc5` | Renderer | renderer: integrate and qualify opt-in NRI Vulkan pipeline libraries |
| `ec2a1ba` | Renderer | renderer: prepare native shader libraries before gameplay with exact dynamic state |
| `18bc3c0` | Renderer | Reduce startup stalls and retire completed swapchain fence ownership |
| `338454a` | Renderer | Defer unused decoded texture alias indexing out of native replay |
| `8c4b6b1` | Renderer | Fix offline toon outline coverage and measure real-time frame stalls |
| `6891f89` | Renderer | Bound real presentation catch-up debt and isolate pacing waits |
| `b361f02` | Renderer | Qualify castle checkpoint pacing and scope Windows multimedia scheduling |
| `80ee461` | Renderer | Reduce castle replay stalls with exact pooled grass spacing columns |
| `33d241f` | Renderer | Reduce recurring grass visibility ordering cost and profile moving frame flow |
| `d619428` | Renderer | Parallelize large grass visibility queries during frame rendering |
| `0435bb4` | Renderer | Document stuttering phase closure and deferred optimization handoff |
| `0b2d848` | UI | Preserve native HUD aspect using exact UI composition ownership |
| `1f5ef90` | UI | Fix centered UI composition and persist minimap layout; supply movie host services |
| `7ee14a1` | AOT | Recover native movie decoder continuations in offline AOT |
| `460960d` | UI | Recover sparse decoder dispatch tables and record native codec evidence |
| `aa5f36d` | UI | Validate native movie decoder against real packets and isolate planar repair |
| `522f185` | UI | Bound background worker quanta and qualify Sheikah Stone playback |
| `d108659` | UI | Preserve Visions UI ownership through shared primitive drawing |
| `dfc3e13` | UI | Record Linux UI issue parity and remaining renderer alignment boundary |
| `0b30a6e` | Menus | Separate standard F1 settings from retained F12 advanced panels |
| `49d6445` | AOT | Document isolated AOT optimization handover and reintegration contract |
| `9892379` | Input | feat(ui): add standard settings navigation and isolated menu input ownership |
| `23961d8` | Menus | feat(ui): replace F1 settings window with retained RmlUi frontend |
| `b36f2c0` | AOT | docs(perf): distinguish non-renderer budget from full-frame throughput |
| `9204018` | Renderer | perf: measure disjoint guest and PICA preparation before Vulkan |
| `791f835` | AOT | perf: identify AOT hotspots with bounded native sampling |
| `b0c4aff` | AOT | perf: qualify and reject AOT cold-read split in game |
| `5207181` | AOT | perf: qualify opt-in call-free AOT region pilot against real game |
| `9496ac7` | AOT | perf: attribute AOT helper costs to real caller stacks |
| `9d12eab` | AOT | diagnostics: measure pre-NRI CPU budget and record optimization stop decision |
| `18a0b56` | AOT | diagnostics: replay real AOT inputs and validate native projection kernel |
| `3fa8010` | AOT | diagnostics: validate whole-pose and triangle native CPU kernels |
| `fd86815` | AOT | diagnostics: specialize and compare closed AOT call families |
| `b04bb31` | AOT | diagnostics: share and validate broader AOT collision cohorts |
| `ecf97a6` | AOT | diagnostics: qualify 303-function AOT cohort with batched replay and tail composition |
| `e7a2913` | AOT | diagnostics: validate cohort activation on live game state and reject costly handoff path |
| `ecea4ca` | AOT | diagnostics: test direct AOT cohort linking without ABI handoffs |
| `a144833` | AOT | docs: summarize direct AOT experiment and measured results |
| `a946f34` | Menus | Restore consolidated F1 menus; preserve split frontend on deferred branch |
| `3f84692` | UI | Recover TopScreen issue fixes after concurrent worktree reset |
| `f0a3b5a` | AOT | Document audited local cleanup and protect active AOT baseline |
| `71a654e` | UI | test: validate recovered TopScreen fixes and document remaining background issue |
| `c35e0a9` | UI | docs: verify issue 40 against user-created live race checkpoint |
| `67513fa` | UI | chore: document expanded obsolete build and trace cleanup |
| `ebd6ba6` | Renderer | fix(renderer): keep native raster initialization outside toon lighting |
| `c1eb992` | UI | docs: record user acceptance of issue 41 with toon enabled |
| `e7913b4` | Input | refactor(input): share SDL controller routing and preserve physical device identity |
| `46c8132` | Input | Integrate Azahar motion and touch into shared controller input |
| `59cfc5c` | Input | Keep motion sensors exclusive to aiming, never free camera |
| `c182ca1` | Input | Enable automatic motion aiming in controller preset and add hardware probe |
| `e84ad86` | Input | Compose stick and gyro aiming with persistent mouse ownership |
