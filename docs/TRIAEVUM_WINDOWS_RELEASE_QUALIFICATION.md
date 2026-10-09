# TriAevum: Windows qualification of the line after alpha.2c

Status: inventory and battery design, 2026-10-04; battery results to 2026-10-05. Branch `qual/windows-next`
(from `perf/aot-levers` `cf4d331`). This record lists every development
intended since the last good release, how each one is proven to work on
Windows, and what is open. It is not a release approval.

## 1. Scope and decisions

- Baseline: `v0.6.0-alpha.2c` (`739f03e`, 2026-09-12), the last published
  release that was not withdrawn. `v0.6.0-alpha.3` and `v0.6.0-alpha.3b` were
  withdrawn (`a9b4477`); their regressions are recorded and corrected in
  `TRIAEVUM_ALPHA2C_TO_ALPHA3_RECONSTRUCTION.md` (issues 45/49, 51, 53 and the
  title-neutral renderer gate).
- Line under qualification: 286 commits, `v0.6.0-alpha.2c..qual/windows-next`:
  142 of `feat/azahar-static-aot-nri` (to `807b66e`, 2026-09-27) and 144 of
  `perf/aot-levers` (2026-09-28 to 2026-10-04).
- Platform: Windows only. Linux, Steam Deck, macOS and Android stay out of this
  qualification; their Python release suites still run because they share code.
- Global illumination (Brixelizer GI and the screen-space GI) is not part of the
  next release. Both stay in the source as experimental paths: off by default,
  not offered by a release build, their tests kept (section 6).
- Outside the line: the uncommitted Azahar platform / static AOT work of the
  main checkout and its preservation branch `feat/azahar-static-aot-nri-recovered`
  (every uncommitted file of the main checkout is identical there, checked
  2026-10-05);
  the macOS (#16) and Linux port proposals.
- Open decision for the user: whether the iMMERSE MXAO module (all rights
  reserved, used under the author's exclusive permission) ships in a binary that
  also links GPL-2.0+ code (`LICENSE_SCOPE.md`, "Exception: the iMMERSE module").

## 2. Sources of intended developments

| Source | What it holds | Treatment |
| --- | --- | --- |
| Commit history (286) | features, fixes, diagnostics, refactors | grouped below by area |
| Recovery snapshots of 2026-09-30 | `recovery/omp-2026-09-28` (`88f935a`, main-checkout work reverted by a worktree reset), `recovery/renderer-2026-09-26` (`a33d1f6`) | 87 % and 95 % of their lines are in the line (restored by `e9dc221` and later work); the remainder is the concurrent Azahar/AOT workstream (section 4.R) |
| Open pull requests | #6, #11, #13, #14, #15, #16, #18 (999sian, PabloVSouza) | integrated selectively on 2026-09-09/10, before alpha.2c, as replacement implementations (`TRIAEVUM_CONTRIBUTIONS.md`); their branches stay open, so their own lines are mostly absent (0-69 %) while their behaviour is in the line. What was deliberately not adopted is listed in section 4.R. #50 (high refresh presets) and the closed #57 are proposals outside the line |
| Local laboratories of 2026-09-16/18 (inventoried 2026-10-05) | `perf/pica-snapshot-version` (`I:/TriAevum-pica-lab`, 3 commits); `perf/aot-independent` (`I:/TriAevum-aot-lab`, 6 commits) and its uncommitted experiment 12; the uncommitted identity-mapped guest memory and semantic-lowering pilots of the clone `I:/TriAevum-aot-optimization`; the clone `I:/TriAevum-source-native-repo` (8 commits, 8 uncommitted files) | Per-range snapshot versions are in the line through `46a2731`: a profiled Field run (900 updates, 85,500 draws) gives the laboratory's own geometry figures (version hits 331,750, misses 51,650, 23.8 MB compared) and compares 9.2 MB of texture bytes against its 75.5 MB. Flag fusion and the guarded hardware VFP path were measured without significant gain (`TRIAEVUM_NON_RENDERER_200FPS_ASSESSMENT.md`); the inline host-float VFP of `perf/aot-levers` succeeds the latter. Experiment 12 is item R6. The identity-mapped memory and semantic-lowering pilots were never measured; the source-native study left the line with `dec3fad` |
| Android donor fork | `I:/TriAevum-public/donor-review-windroid` (WINDROID-EMU, 38 commits since alpha.2c) | examined, not integrated (`TRIAEVUM_PROJECT_HANDOVER.md` section 13); Android only |
| Unreachable objects and reflogs | commits since alpha.2 that no branch holds | 28 later pushes of the macOS proposal (#16), two empty stash index commits, three reflog-only commits superseded by amended or re-recorded ones: nothing to integrate |
| GitHub issues | user reports since alpha.2c | a fix claimed by a commit or document needs a scenario check (section 4); the open issues are mapped in section 8 |
| Qualification documents | `TRIAEVUM_TOPSCREEN_ISSUES_25_32_39.md`, `TRIAEVUM_ISSUES_40_43.md`, `TRIAEVUM_ISSUES_45_46.md`, `TRIAEVUM_ISSUE_48_TITLE_START.md`, `TRIAEVUM_ISSUE44_HD_FONTS.md`, `TRIAEVUM_RUNTIME_PROGRESS_QUALIFICATION.md` | their open acceptance items become battery entries |

Line survival (lines a commit added that still exist anywhere in the tree,
moves and splits included) was measured for every commit: below 70 % only for
generated shader headers replaced by binary packs (`86bb942`), deliberate
reverts (LAUNCHPAD `21bf907`, shadow receiver history `c31bd43`), the split
F1/F12 frontend kept on `feature/deferred-f1-f12-menus` (`a946f34`), GI
iterations, and the TextureCopy GPU decode of 2026-09-23 (`85fefa8`, `0765e0c`,
`602d10e`, `6cc19cc`: 18-27 %), which section 4.I checks.

## 3. Battery layers

| Layer | Content | Runner |
| --- | --- | --- |
| L1 build | clean configure and build of the product (`TriAevum.exe`), the title module and every test target; an expected target list, so a test that is not built fails the battery | battery runner |
| L2 unit | every host test executable, the GPU test project (with its generator `--check` modes), the Python suites (`tools/triaevum_release`, `tools/oot3d/native_a32_runtime`, `tools/renderer/tev_program`, other tool suites) | battery runner |
| L3 F1 | actual-widget smoke (`triaevum_f1_settings_smoke`) | battery runner |
| L4 identity | deterministic Kokiri captures on eight renderer profiles against the reference set, `runtime.json` structural diagnostics, GI-off identity | `qualify/baseline` gate, kit |
| L5 scenarios | savestate or save-file scenarios driven by recorded input, with framebuffer and runtime assertions (section 5) | battery runner, scenario files |
| L6 package | release build options, Forge installation into a deep path without compilation, release audit and layout | battery runner |
| L7 user | short manual checklist for what cannot be automated (feel of input devices, audio, long play) | this document |

Every entry below names the layer and the check that proves it. Status values:
`pass` (proven on this line), `open` (no proof yet), `fail`, `lost` (intended,
absent from the line), `experimental` (excluded from the release).

## 4. Inventory

### 4.A Release, Forge and packaging (Windows)

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| A1 | Forge counts embedded renderer shader artifacts during preparation | `5d13fa7` | L2 release suite; L6 install | open |
| A2 | Atomic writes keep temporary paths inside the legacy limit (Forge in a deep root, #53) | `4a09933`, reconstruction 3.1 | L2 `test_common` path capacity; L6 install into a padded root | open |
| A3 | Forge prepares the renderer's startup programs during installation | `6d2f40a` | L6 install, then first launch reports prepared programs | open |
| A4 | USA hint movies get the names the EUR module opens | `e5fbc5a` | L2 Forge suite; L5 movie scenario on a USA install (if a USA input exists) | open |
| A5 | Developer PGO profile accepted for title modules; a profiled module is published with its profile | `41cc7b5`, `924d93a` | L2: `prepare_release --title-profile` stores the profile in the translated title source and the audit requires it for a profiled module (`test_precompiled_release`). L6 confirms the release module build options | pass (L2); L6 open |
| A6 | iMMERSE module isolated with its own licence, release layout maps both licence files | `b94b3b8` | L2 release audit tests; L6 audit | pass (gate `verify-immerse`) |
| A7 | Source-archive UI contract reads the CMake area files | `27e5a10` | L2 | pass |
| A8 | Forge's portable shader corpus matches the release's program generator | `TRIAEVUM_BUNDLED_FORGE_SHADER_CORPUS.md` | L6: regenerate the corpus with the release runtime. The corpus used by the qualification runs (`expanded-union.o3ps`, 2026-09-10) resolves 0 of 29 programs of the Kokiri walk: its modules are keyed by the generated source text, which the preparation work changed. With hybrid preparation a stale corpus costs no stall (S17), only background compilation on a first launch. 2026-10-07: `regenerate_shader_corpus.py` rebuilds it from the private raw inputs with the runtime's own importers (fixing a fragment identity collision on the way): 989 modules, Kokiri 18 of 29 resolved, 18 of 18 in Authentic; the rest are effect variants compiled in the background ([regeneration](TRIAEVUM_SCENARIO_SHADER_PREPARATION.md#regeneration-with-the-current-runtime-2026-10-07)) | open (L6): regenerate with the release runtime |

### 4.B Startup, shader preparation and stutter

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| B1 | Precompiled native, temporal, toon, uber, caster and pass families; no shader compilation in the product | `247f234`..`4d0e39b`, `86bb942`, `271b9a5` | L2 generator `--check`; L5 S17 counts pass compilations. The raw-copy row fix (`ae6fa94`) changed the texture copy derive shader without regenerating `builtin_pass.spvpack`: it compiled at every launch until `bb23340`; the eight generator checks pass. The raw-copy encode shader is outside the catalogue and compiles at the first raw copy of a cold cache | pass (L2, L5 S17); encode shader open |
| B2 | Pipeline identity excludes dormant state; shared normalized pipelines; bounded retirement | `801a359`, `d42143c`, `aaa3530`, `a9d9441`, `fd9195a` | L2 pipeline tests; L4 | pass (L4) |
| B3 | Background program preparation, always-available programs, hybrid default | `4151f1a`, `a430ab8`, `271b9a5`, `44b59e3` | L4; L5 S17 from an empty cache: no draw waits for a program (5,475 always-available draws), slowest measured frame 43.9 ms. The Vulkan validation layer found the startup links invalid (separate-sampler layout against a combined-sampler base; superset programs linked with a vertex program lacking their inputs 7-10) and the seam binding unwritten for always-available surface programs; fixed in `6ad23a5`, no validation error left on five profiles | pass (L5 S17, validation) |
| B4 | Pipelines prepared in the background are persisted and reused | `038dcf9`, `b644be7`, `390d918` | L5 S17 warm: no compilation (72 cache hits), 23.5 MB of pipelines loaded, slowest frame 42.2 ms | pass (L5 S17) |
| B5 | Initial preparation before gameplay | `743d05a` | L5 S17: preparation completes (39 parts, no failure, no guest wait warm or cold); its first frames run under the screen, outside the measured window | pass (L5 S17) |
| B6 | Startup stalls reduced; swapchain fence ownership retired | `18bc3c0`, `e6490d7` | L5 timing receipts | open |

### 4.C Renderer core

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| C1 | Parametric PICA TEV, lighting, fog, alpha, texture selection, procedural texture with GPU parity | `12b2c97`..`054bb5b` | L2 GPU parity tests | open |
| C2 | Offline-translated native vertex family; title-owned precompiled vertex programs | `8727eb4`, `91d4c5b` | L2 `build_vertex_artifacts --check` (needs the private SHBIN copies) | open |
| C3 | Descriptor pools grow on demand; live shader contracts promoted after replay | `29b8bdb`, `8079f6c` | L2; L5 long gameplay scenario | open |
| C4 | Shared 3DS renderer stays title-neutral (boundary gate) | `ba1f200`, reconstruction 3.4 | L1 `verify_renderer_3ds_cross_title_boundary` | open |
| C5 | Scene completion exposed only before native UI; title and menu draw domains (#48) | `d363d36`, `16be838` | L5 title Start scenario | open |
| C6 | Per-draw allocation, register-hash and lighting-decode levers | `83c7be4`, `2c545df`, `754ccb3`, `8cdf3ca` | L4 identity | pass (L4) |
| C7 | Effects reconstruct depth with the draws' projection; one shared geometry guide | `0dfc904`, `567281a` | L4; L2 effect-graph tests | pass |
| C9 | Vulkan validation of the release profiles (Enhanced, point and directional shadows, toon outline, plain path, title intro) | 2026-10-05 sweep | Before `6ad23a5`: three error classes on every profile (B3); after: none, message limit disabled; remaining warnings are unused vertex attributes, masked guide outputs and unread vertex outputs | pass |
| C8 | Toon: uniform appearance, native raster initialization outside toon lighting, offline outline coverage | `8735f46`, `ebd6ba6`, `8c4b6b1`, `6ad3d83` | L4 `cfg-gg-outline*`; L2 `oot3d_toon_outline_gpu_tests` (one failing case, section 7) | open |

### 4.D Presentation and pacing

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| D1 | Bounded presentation catch-up debt, isolated pacing waits, one interactive limiter, native clocks | `6891f89`, `b361f02`, `ab6b485` | L2 presentation clock tests; L5 timing scenario | open |
| D2 | One visual frame per guest frame when the host falls behind | `b09e3cb` | L5 throttled scenario. Its split re-executed raw-copy dependency work: under Original30 the revelation stopped at its first copy (run frame 2473, every output size). Fixed 2026-10-05 (each closed frame carries the executed dependency prefix; `oot3d_native_pica_vulkan_bridge_tests`); L5 S09 Original30 passes the copies. Its split also treated the TopScreen frontend menus, which the guest draws onto the upper target after the upper snapshot, as the next frame's work: from 2026-09-30 File Select and Name Entry were drawn over by the next frame and never shown (alpha.3b shows them, A/B of 2026-10-05). Fixed 2026-10-05: draws on the relocated menu canvas continue their frame (`FinishFrames` continuation viewports, only while that relocation is active; `oot3d_native_pica_vulkan_plan_tests`, frontend composition tests); L5 S01 and S28 show the menus again | fixed; throttled scenario open |
| D3 | Parallel presentation (render command stream, worker) on by default | `fc5408f`..`f5fff67` | L4 identity; every L5 run uses the worker (`benchmark_window.parallel_presentation`); resize, fullscreen and F1 toggles under the worker are not automated. Throughput kept (ABBA 2026-10-05, Field, Interpolated2x, no-present, PGO module): 3.478 ms per presented frame for the qualified line against 3.468 ms for the build before its fixes (paired ratio 1.004, pairs 1.030 and 0.978, guest contracts equal), the 3.46 ms recorded for the worker on 2026-09-29 | L4/L5 pass; throughput kept; display changes open |
| D4 | Fullscreen lifecycle (the alpha.2c fix) and display changes | alpha.2c, #3 | L5 fullscreen/windowed toggles | open |
| D5 | Temporal jitter phase counts frames that drew the scene; temporal history across graph invocations | `e550c18`, `b49fa74`, `4120c4e` | L4; L5 pause/unpause scenario | open |

### 4.E UI composition, HUD and TopScreen

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| E1 | Native HUD keeps its aspect with exact UI composition ownership | `0b2d848` | L5 S03 HUD at 4:3, 16:9, 21:9 (#25). Failed on this line: `ba1f200` composed the TopScreen buttons, the minimap and dialogue boxes as screen content (enlarged, moved to the output edges, cut; A lost at 21:9). Fixed 2026-10-05: only a wash or a redraw of the copied frame whose geometry covers the viewport is screen content (reconstruction doc 7.3, correction). S03 captures match the placed layout of alpha.3b at all three sizes; battery 2026-10-05: S03 15/15 checks | pass (L5 S03) |
| E2 | Centered UI composition; minimap layout persists (and survives savestate reload, #32) | `1f5ef90`, issues doc 25/32/39 | L5 minimap marker scenario, fresh entry and reload. Minimap placement failed with E1 and is fixed with it; persistence and reload still unproven | open |
| E3 | Visions UI ownership through shared primitive drawing | `d108659` | L5 S10 (Sheikah Stone, 4:3, 16:9, 21:9). The Visions header and panel were cover-scaled with E1 and are back on the canvas since the fix, at all three sizes. Movie frames: by the user's choice (2026-10-05) shown whole at 5:3 with black bars (above and below at 4:3, beside at 16:9 and 21:9), no longer cropped to cover; no lit pixel outside the picture in any movie frame. Only opaque pictures (no texture alpha) get bars; covering overlays with alpha keep the cover canvas | fixed (L5 S10) |
| E4 | Consolidated F1 settings; F1 toggles from the key event (#51); Steam Input pads pass through | `a946f34`, `816fe91` | L3; L5 F1 tap scenario | open |
| E5 | TopScreen #40 stamina/timer overlap, #42 mounted B counter, #43 boots hotkey repeat (recovered after the 2026-09-17 reset) | `3f84692`, `71a654e` | L2 TopScreen profile tests; L5 Epona and boots scenarios | open |
| E6 | Game Over choices restored (#46): directional/A, save-yes, mouse/touch | `f89d3fb`, issues doc 45/46 | L5 S08: directional input and A drive the Game Over and save questions (Save gives "Progress saved", Don't save "Progress was not saved"), on the native canvas at three sizes (2026-10-05, references). Mouse/touch not yet in a scenario | directional/A pass (L5 S08); mouse/touch open |
| E7 | Native UI kept out of the upscalers; TAA/SMAA resolved before native UI | `85c780f`, `cd6e550`, `d39aa14` | L4; L5 S14 (DLSS Ultra Performance at 21:9, HUD and pause page): the scene is reconstructed and the native UI is laid over the output on every scene frame (2026-10-05: 157 DLSS passes and 156 overlays in 300 frames; captures reproducible byte for byte). Closing the pause shows the HUD over the page for two frames, with and without DLSS alike (the game's own transition). FSR and TAA/SMAA are not in a scenario yet | pass (L5 S14: DLSS, FSR 2, TAA, SMAA 1x) |
| E8 | F2 native lighting toggle and Navi fix | `e9dc221` | L5 | open |
| E9 | The title's normal / Master Quest choice on the single screen (#28), as TopScreen 2.1.1 shows it | 2026-10-05 | The choice is `COmoteUraSelector`, a lane of the root pause UI that runs in game mode 1, so the frontend presentation (File Select, Name Entry) never covered it and the TopScreen START routing turned START into a touch on the Master Quest card. From the moment its panels enter until they leave it now owns the frontend presentation in both profiles (`oot3d_native_ui_lifecycle_bridge_tests`): TopScreen relocates the native menu onto the upper screen without its own background (common_bg01, removed like File Select's backdrop by the user's choice; the title logo shows between the cards), the OoT3D profile presents the lower screen; D-pad, circle pad, A/START and touch/mouse act natively. L5 S28 (right, A, Master Quest file selection, 16:9 and 21:9) and S01 | pass (L5 S28, S01) |

### 4.F Fonts

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| F1 | HD native QBF fonts in the TopScreen at 480p and above, coverage atlas, Forge preparation from the TopScreen archive (#44) | `3d38ef1`, `d28ba12`, `b58a83f`, `8f802cc` | L2 `test_qbf_font`, `verify_hd_fonts.py`; L5 dialogue scenario (acceptance list of the #44 document) | open |

### 4.G Movies

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| G1 | Native movie decoder continuations and dispatch tables recovered in offline AOT; Sheikah Stone plays, completes or cancels and returns to gameplay | `7ee14a1`, `460960d`, `aa5f36d`, `522f185`, `1f5ef90` | L5 Sheikah Stone scenario | open |

### 4.H Input

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| H1 | Shared SDL controller routing with physical device identity | `e7913b4` | L2 `three_ds_sdl_controller_tests`, input suites | open |
| H2 | Azahar motion and touch in the shared controller input; motion only for aiming; automatic motion aiming in the preset; stick + gyro aiming with mouse ownership | `46c8132`, `59cfc5c`, `c182ca1`, `e84ad86` | L2; L7 device checklist | open |
| H3 | Mouse capture policy | — | L2 `mouse_capture_policy_tests` | pass |

### 4.I TextureCopy and cutscenes (#45, #49)

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| I1 | Ordered raw TextureCopy reaches the Sheik revelation; completion obligation survives a savestate | `0bcd22d`, `e9a14a7` | L2 visual savestate tests; L5 S09 revelation (Original30 and interpolated). Original30 failed at the first copy (D2) and passes since the fix; the flashback covers every output column at 4:3, 16:9 and 21:9 and is drawn with the scene's mapping (the former cover canvas zoomed it outside 5:3). Battery 2026-10-05: S09 Original30 21/21 checks. Mid-flashback savestate restore still to run | Original30 pass (L5 S09); interpolated checked by hand; restore open |
| I2 | Raw copy readback cost cut; GPU encode; guest-access watch | `99e2005`, `4f58878`, `7dd875c`, `e91a360` | L2; L5 revelation timing | open |
| I3 | Copied textures decoded on the GPU (2026-09-23 work, 18-27 % of its lines in the line) | `0765e0c`, `602d10e`, `6cc19cc`, `c5b6bb0` | establish whether a later commit replaced it on purpose; L5 revelation | open |
| I4 | Canonical GPU-copy visibility preserved across title execution | `bcec0f6` | L5 revelation; L2 | open |
| I5 | Orientation of the raw-copy redraw | found and fixed 2026-10-05 | The flashback was the left-right mirror of the scene it copies (luminance correlation with the scene drawn under it, redraw skipped: 0.03 as captured, 0.998 mirrored). The raw-copy path read attachment row y as PICA memory row y; attachments hold window rows bottom-up while memory stores the top window row first (Azahar: framebuffer writes window row y at memory row height - 1 - y, the texture unit reads t at row height - 1 - t). `EncodePicaFramebuffer`, its GPU form and the GPU copy derivation now reverse rows (L2: transfer tests, GPU derivation in both row orders). Interpolated revelation, every 20 frames: each flashback capture is the exact mirror of the previous build's (correlation 1.000 mirrored), dialogue boxes unchanged; Link stands in the left part of the frame looking right, towards the castle, as the user remembers the original | fixed |

### 4.J Lighting, shadows, materials, AO, AA and upscalers

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| J1 | Scene lit by native environment directionals; queued atmosphere appearance preserved; native brightness independent of light gains | `6e80d2e`, `6c07177`, `5b530d5`, `bc4f8c9`, `b0772ec` | L4; L5 day/night scenario | open |
| J2 | Key light from the scene's environment lights, its colour from the main light; continuous light under animated time; shadow keyframes follow a moving light | `9669831`, `d1691e8`, `0a3a357`, `5a83492` | L5 S15 (`analyze_scene_lighting.py`): Hyrule Field with the clock running, 899 moving guest frames without jump or casting from below the horizon, the shadow following the light in 1,679 of 1,679 frames without stall, cut or construction over budget; title intro through dawn without jump or casting from below the horizon, the shadows waiting for their first keyframe after each scene cut (15 frames, the P2 design). The title intro's validation errors were the preparation's (B3) | pass (L5 S15) |
| J3 | Directional shadows: current sample's casters, alpha-tested casters keep their cutout; point lights cast cube shadows | `ab1b32a`, `e4930f7`, `d92fe95` | L4 `cfg-points`; L5 | pass (L4) |
| J4 | Grass VSM shadows, actor shadow cache | `e9dc221` | L4; L5 | open |
| J5 | Editable PBR packs and native UV seam softening | `c9138cb` | L2; L5 | open |
| J6 | AO providers: FidelityFX CACAO, multiscale GTAO; native AO normals and texture relief | `19a1296`, `5d59274`, `567281a` | L2 AO guide tests; L4; L5 | open |
| J7 | MXAO (iMMERSE module): temporal, depth/face normals, options, debug views | `7d2a889`, `d39aa14`, `72c9eea`, `93fcc1f`, `e543598`, `1c71763` | L2 `mxao_gpu_tests`; L4 `cfg-su-native` | pass (L4), release decision open |
| J8 | Upscaler output is the display; Ultra Performance renders 1/3; selectable DLSS model, the Enhanced preset uses the transformer | `af6529a`, `8abc0eb`, `7cc023f` | L2 `test_dlss_runtime_validation`; L5 | open |
| J9 | Scene changes no longer recreate the temporal upscaler | `2f53274` | L5 house-entry scenario: one upscaler instance per stream | open |
| J10 | Scene effects use the rendered sample camera; SSSR per surface; reflections analyse environment terms once | `db2ddd8`, `a025d4d` | L4 | open |
| J11 | Savestate restore keeps colour targets at the saved scale | `481290d` | L5 savestate scenario at a non-native scale | open |

### 4.K Grass

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| K1 | Exact pooled spacing columns, visibility ordering, parallel visibility, stable selection, moving interaction cells | `80ee461`, `33d241f`, `d619428`, `48bd779`, `94bee2f` | L2 grass tests; L4 | open |
| K2 | Exact grass motion vectors | `a179c21` | L2 `grass_motion_gpu_tests`; L4 | pass (L4) |

### 4.L Audio

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| AU1 | Deferred PCM and ADPCM buffers observed completely | `aada562` | L2 `oot3d_native_audio_tests` (needs ROM-derived fixtures, section 7); L7 | open |

### 4.N AOT title module

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| N1 | Verified memory epochs and replay baseline preserved; contracts tested; synchronous AOT work attributed | `46a2731`, `ffccab0`, `8d891cf` | L2 AOT runtime suites | open |
| N2 | Host title helpers with exact single-page memory paths; inline host-float VFP; frequent SVC names without streams | `ad030db`, `b6ceda7`, `fca3e07` | L4 identity with the module that ships; module build options recorded by L6 | open |
| N3 | Opt-in memory-region pilot and rejected AOT experiments stay inactive | `df0d82f`, diagnostics of 2026-09-16/17 | L6 release build shows them off | open |

### 4.O Diagnostics and developer tools (must be inert in the product)

GPU gap trace (`5b0ee4a`), graphics settings schedule (`758e0a1`), no-present
throughput mode (`ae8d00c`), environment light trace (`e2f1ed4`), GI dumps
(`ef413b8`), AOT sampling and cohorts. Check: L4 identity with every developer
variable unset; L6 confirms none is enabled by the release configuration.

### 4.P Global illumination (experimental, excluded)

Brixelizer GI phases 1-5 and fixes (`e805e9b`..`7871fab`), the GI setting and
F1 control (`1a55980`), the screen-space GI M1-M8 (`ccda246`..`cf4d331`). Check:
L2 GI suites keep running in developer builds; L6 proves the release build
offers no GI (section 6). Pre-existing finding: with either GI mode on, a shared
pass samples an image in `GENERAL` layout with a `SHADER_READ_ONLY_OPTIMAL`
descriptor (validation `imageLayout-00344`, `None-09600`); not a release item
while GI is excluded.

### 4.Q Reorganization (behaviour-preserving)

SPIR-V binary packs, prunes of unused sources, topic splits of the window,
Vulkan backends, CTR host, TopScreen profile and audio, CMake area files,
unreferenced product code dropped (`86bb942`..`fc473e0`). Check: L4 identity
(gates p3* to p5m8), L1 builds, L2. Status: pass.

### 4.R Integrated contributions, not-adopted parts and lost work

No commit, pull request or recovery snapshot holds HUD/UI code that is missing
from the line: every HUD/UI commit since alpha.2c survives at 91-100 % and the
open pull requests were integrated as replacement implementations. HUD/UI work
reported lost is therefore either work that was never committed (a reset
before a commit) or a behaviour regression of code that is still present. The
L5 HUD/UI scenarios detect the second; the user's list of missing behaviours
drives the first.

| ID | Development | Evidence | Check | Status |
| --- | --- | --- | --- | --- |
| R1 | Product-mode TopScreen item getters, slot resolution and Items-page ZR/ZL assignment (#39) | PR #13, integrated as the item dispatch module (`7617c8a`, `TRIAEVUM_TOPSCREEN_PRODUCT_ITEM_DISPATCH.md`) | L2 `oot3d_top_screen_mod_profile_tests`; L5 S04 (assignment, bow ammunition, hookshot) | open |
| R2 | PR #13's press latch, not adopted: a ZL/ZR tap shorter than a guest refresh, or released before the game polls, can be lost; the document leaves it to the input owner | `TRIAEVUM_TOPSCREEN_PRODUCT_ITEM_DISPATCH.md` | L5 S04 with one-frame taps | open |
| R3 | Ocarina song guide and D-pad ownership in a dedicated title UI module (PR #18's evidence; its heuristics not adopted) | `TRIAEVUM_TOPSCREEN_OCARINA_OWNER.md` | L2 TopScreen ocarina tests; L5 S07 | open |
| R4 | Display-change rejection reason in the F1 display status (PR #14, `dd7539e`) | `TRIAEVUM_CONTRIBUTIONS.md` | L3 actual-widget lifecycle | open |
| R5 | Remainder of the 2026-09-28 / 2026-09-26 recovery snapshots | `88f935a` (13 % absent), `a33d1f6` (5 %) | checked line by line on 2026-10-05: the Azahar static AOT notes, the module's SHA-256, TAM reader tests and the Azahar NRI interop are held by `feat/azahar-static-aot-nri-recovered`; the out-of-line typed-memory and block-entry gates are the cancelled second candidate of `TRIAEVUM_AOT_CPU_OPTIMIZATION.md` (hot/cold separation: no qualified result, its compiler-attribute header is not in the snapshot); the renderer lines are earlier forms that later work rewrote (single-layout VSM lookup, static depth cache and shadow history release before the P2 keyframes, older shader salts and schema versions), while the static depth cache per keyframe layout and the shadow producer's late `BeforeUi` anchor are in the line | Azahar/AOT part out of the line; renderer part superseded |
| R6 | Closed-loop lowering of proved copy loops (`0x0040BDA8`, `0x004210A4`, `0x00466E7C`): one `memcpy` with exact registers, flags, budget and write generation, the checked loop as fallback; differentially simulated on 2,664 accepted and 1,336 refused ranges without a mismatch | uncommitted in `I:/TriAevum-aot-lab` (2026-09-16, `docs/TRIAEVUM_AOT_OPTIMIZATION_RESULTS.md` experiment 12): median CPU ratio 0.974 over 6 pairs (5 faster), measured against the module of that day; the levers protocol puts the A/A spread at about +/-5 % per pair. Its inline region accessors advance only the global write generation: on the line the lowered stores must also stamp the per-page write generations that the snapshot caches read | port with that stamping and a snapshot-cache test, then ABBA against the PGO module with the `TRIAEVUM_AOT_LEVERS_RESULTS.md` protocol | open: candidate, not measured on the line |

## 5. Scenarios (L5)

Each scenario names its input (savestate or save file, never distributed), the
recorded input, the profiles it runs under and its assertions. Inputs are
private and read only; outputs go to a scratch directory.

The battery runner's `scenarios` layer executes them
(`qualify_windows_battery.example.json` shows the configuration): one run per
output size, as `TriAevum.exe`, with deterministic timing (`interpolated` or
`original30`). Assertions per run: exit code, number of captures, accepted
reference captures (byte identity; a missing reference is "not run"),
full-width effects (every column grey), black bars around movie frames (every
pixel outside the fitted 5:3 picture black), and bounds on per-frame renderer
diagnostics (`whole_target_ui_draw_count`, `screen_content_draw_count`,
`own_picture_draw_count`, `picture_bar_rect_count`,
`picture_overlay_draw_count`). Implemented so far: S03 (HUD at 960x720,
1280x720 and 1720x720: no screen content and no picture, references), S09
under Original30 (three sizes: the copies pass, the flashback covers every
column, no opaque picture, references) and S10 (Sheikah Stone movie at three
sizes: picture draws, black bars, references), S04 (pause roundtrip and pages at three sizes) and
S14 (DLSS with the native UI overlay), S01 (title, late and early Start) and S08
(Game Over, Save and Don't save). Run of 2026-10-05 on the build
with the upright copy and the movie bars: 84 of 84 checks against the
references accepted from it. The S09 flashback reference changed only by the
mirror: each new capture equals the previous reference flipped left-right,
pixel for pixel; S03 and S10 reproduced their references byte for byte.
S04 and S14 (100 of 100 checks) reproduced the reviewed probe captures byte
for byte, DLSS included, before their references were accepted.

| ID | Scenario | Input | Covers |
| --- | --- | --- | --- |
| S01 | Boot, title, Start during the title presentation, file select, load a save | none (fresh boot) and a practice save | B5, C5, #48. In the battery since 2026-10-05: Start after the presentation (`title_start_late_file_select.json`) reaches the game selection at three sizes (references). Start during the presentation (`title_to_file_select.json`, guest frames 100 and 300) no longer stops the run (#48's failure). It is honoured: it opens the normal / Master Quest choice, which waits for a choice. The earlier reading "the title is still shown, as with alpha.3b" was that choice drawn only to the lower screen (#28, E9); since the fix the captures show it with the normal version highlighted (references, `version_choice_*` metrics). The second Start of the late timeline confirms the highlighted normal version; before E9 the TopScreen START routing turned it into a touch on the Master Quest card. Loading an existing save in the battery since 2026-10-05 (S01-load-save). |
| S02 | Kokiri walk, eight renderer profiles (the L4 gate) | `qualify/ref-kokiri2` | C, J, K, Q |
| S03 | HUD at 4:3, 16:9 and 21:9; minimap markers on fresh entry and after reload | practice saves with map/compass | E1, E2, #25, #32 |
| S04 | Pause menu pages, item numbers, map screen, ZL/ZR assignment and use, one-frame taps | practice saves (adult with bow/hookshot) | E2, R1, R2, #39. In the battery since 2026-10-05: the Start roundtrip and the Map, Gear and Items pages from the HUD checkpoint at three sizes (pages on the native canvas, no screen content); assignment and one-frame taps still open |
| S05 | Epona: stamina and timer, mounted B counter | issue 40-43 checkpoints | E5, #40, #42 In the battery since 2026-10-05 (race and mounted). |
| S06 | Boots hotkey repeat | `boots-held` checkpoint | E5, #43 In the battery since 2026-10-05. |
| S07 | Ocarina performance UI, song browsing | practice save with ocarina | R3 |
| S08 | Game Over: continue/quit by directional/A, save-yes, mouse/touch | Game Over checkpoints | E6, #46. In the battery since 2026-10-05: Save and Don't save paths from the zero-health checkpoint at three sizes |
| S09 | Temple of Time revelation and the mid-flashback savestate, native 30 and interpolated 2x | issue 45/46 checkpoints | I1-I4, #45, #49 Interpolated with a mid-flashback restore in the battery since 2026-10-05. |
| S10 | Sheikah Stone movie: play, complete, cancel, return | Kokiri state near the stone | G1 |
| S11 | Dialogue with HD fonts at 480p and above, TopScreen on and off | practice saves | F1, #44 |
| S12 | F1 tap toggle, F2 native lighting, capability-safe Off controls | any gameplay state | E4, E8 |
| S13 | Fullscreen and windowed toggles, display change rejection | any | D4, R4 |
| S14 | DLSS and FSR profiles: UI at output resolution, house entry keeps one upscaler | `play-kokiri` style state at a house door | E7, J8, J9. In the battery since 2026-10-05: DLSS Ultra Performance with the HUD and a pause roundtrip at 21:9 (upscaler and UI overlay counted, references); FSR 2, TAA and SMAA 1x with the same input since 2026-10-05 (references); the house entry is open (its private state is gone) |
| S15 | Night/dawn light and shadows | open-field checkpoint, clean boot | J2. In the battery since 2026-10-05: `S15-field-light` and `S15-title-intro-light` with `lighting:` metrics (section 4.J) and references |
| S16 | Savestate save/restore at a non-native scale | Kokiri | J11 |
| S17 | Cold and warm cache start: preparation, no runtime compilation, persisted pipelines | fresh cache directory | B1, B3-B5. In the battery since 2026-10-05: `S17-shader-preparation`, cold then warm from the cache it left, without captures, bounded by log and runtime metrics (section 4.B) |
| S18 | Slow host: one visual frame per guest frame | Kokiri with a throttled host | D2 |
| S19 | Chamber of Sages with the user's toon settings | `sages-user/issue41.oot3dsav` | #41. In the battery since 2026-10-05 |
| S20 | Real-time pacing: Kokiri walk, Enhanced, interpolated on the wall clock after a warm-up; VSync on and off | Kokiri reference state and walk | D1, #30, #55. In the battery since 2026-10-05: FPS, frame-time p99 and `dsp_audio.output_underruns` bounds |
| S23 | Save (Game Over), quit, restart on the same save data and cache | zero-health checkpoint | #23. In the battery since 2026-10-05 |
| S28 | Title normal / Master Quest choice: right highlights Master Quest, A confirms it, its file selection follows | fresh boot, save data with Master Quest unlocked | E9, #28, D2. In the battery since 2026-10-05 at 16:9 and 21:9 (references; `version_choice_captures`, `version_choice_last_selection` 1, File Select reached). A touch or mouse click on a card selects and confirms that version (probe from the choice savestate) |

## 6. Global illumination out of the release

Implemented 2026-10-05 (`074bfa6`): `THREE_DS_RECOMP_OFFER_EXPERIMENTAL_GI` (OFF) decides whether a build offers the two GI modes. Without it both capabilities report "experimental, not part of this build", F1 keeps them disabled with that reason, and the rendering snapshot renders a GI mode Off when its capability is unavailable: the settings layer validates capabilities only on edits, so a GI mode loaded from a configuration rendered anyway (373 and 195 ms of GI work in 90 frames before, 0 after). The runtime reports `capabilities.experimental_global_illumination`; `prepare_release` refuses a runtime that offers it and the audit rejects a contract that does not exclude it or selects GI by default. F1 smoke 8,461 assertions; all L5 references unchanged.

Requirement: a release build offers no GI mode; the GI code stays in the tree
and in developer builds. Design (to implement): a build option for the
experimental GI paths, off in the release configuration; when off, both GI
capabilities report "experimental, not part of this build", the settings layer
normalizes any persisted GI mode to Off (it already does for an unavailable
capability) and F1 shows the capability-safe Off control with that reason. The
release audit checks the option and the default configuration.

## 7. Red items and their outcome

From the last full host-test run of this line before the battery (2026-10-04,
M8 gate): 88 of 96 test executables passed. Outcome after the battery's first
runs (`8b16cf7`: CTest registers every root, module and release test
executable; 1,124 tests):

| Item | Observation | Outcome |
| --- | --- | --- |
| `sdl_video_driver_policy_tests` | crashed (exit 139): SDL's Windows setenv deletes an empty variable and the test built a `string_view` from null | fixed (`8b16cf7`) |
| `three_ds_recomp_tests` | `EventSystem.CallEventUpdatesCallerMetadata`, `CallEventIncrementsCaller` failed: callers are recorded only in `_DEBUG` builds | fixed (`8b16cf7`) |
| `triaevum_native_module_loader_tests` | exit 1: the test needs the mock module's path as its argument | fixed (`8b16cf7`) |
| `oot3d_native_a32_process_tests` | asserted the interpreter dispatcher's registry, which whole-AOT product mode does not use | fixed (`8b16cf7`, configuration-aware) |
| `oot3d_native_audio_tests` | needs ROM-derived fixtures | reads `OOT3D_TEST_EXTRACT_ROOT`, passes (`8b16cf7`) |
| `oot3d_native_mass_aot_tests`, `oot3d_native_pica_frontend_tests` | were not built | built where their generated inputs exist; frontend test repaired (`8b16cf7`) |
| `oot3d_toon_outline_gpu_tests` | `Oot3dGrassLodGpu.IndexedVertexDecoderMatchesExpandedStreamForEveryTopology`: `vkCreateComputePipelines` failed for a test shader with a large constant index array | fixed: indices and ranges are input words of a storage buffer; the four GPU tests of the executable pass |
| `oot3d_game_module_smoke` | needs a Forge-made `game.tam` and `content.tap` | skipped (77) until the L6 installation provides them |
| Revelation under Original30 | stopped at the first raw copy (D2) | fixed 2026-10-05 |
| TopScreen HUD, minimap, dialogue at 4:3/16:9/21:9 | composed as screen content since `ba1f200` (E1) | fixed 2026-10-05 |
| Vulkan validation of the release profiles | three error classes from the hybrid preparation (C9) | fixed (`6ad23a5`); battery 247/247 and L4 6x12/12 identical |
| Texture copy derive shader outside the pass catalogue | regression of `ae6fa94`, runtime compilation at every launch | fixed (`bb23340`) |
| Persisted GI mode rendered without its capability | found while gating GI (section 6) | fixed (`074bfa6`) |
| `pipeline_prepare` contract test | stale outline guide mask since `567281a`; the project is not built by the battery | fixed (`6ad23a5`); add the helper build to L1 |
| L4 identity after the UI composition fix | the first gate run was stopped by the host for low memory after four pre-fix profiles | rerun 2026-10-05, six profiles x 12 Kokiri frames: every difference lies in the two HUD elements the fix moved (TopScreen buttons, minimap: 100 % of the union of differing pixels); scene pixels identical. Pass |
| Vulkan 1.2 entry points imported eagerly (#12, #35) | issue 12's loader report (Windows 10 21H2, Vulkan loader 1.1.97) names `vulkan-1.dll!vkGetSemaphoreCounterValue`, `vkSignalSemaphore` and `vkWaitSemaphores` as unresolved; the line's `TriAevum.exe` (2026-10-05) still imports all three eagerly, so the process cannot start on such a loader (0xC0000139) | fixed 2026-10-05: commands newer than Vulkan 1.0 come from the loader at run time (`renderer3ds/vulkan_commands.h`; the FidelityFX backend's two direct Vulkan 1.1 calls go through its stub), `TriAevum.exe` and the Forge helper import only Vulkan 1.0 and WSI commands, and the release audit rejects any other `vulkan-1.dll` import (`vulkan_imports.py`). DLSS, FSR 2, HUD and S17 scenarios unchanged (55 of 55); the D3D12 NGX frame bridge still verifies its shared fence |

## 8. Open GitHub issues (2026-10-05)

44 issues are open. 25 report a malfunction and are mapped below. The others
are feature or port requests (#2, #8, #9, #10, #22, #33, #34, #36, #47, #54,
#59), project or packaging requests (#1, #29, #38), a licence dispute (#52), a
question answered by alpha.2 (#21; one comment mentions a fullscreen crash
without details), the HD font request implemented as F1 (#44), and ROM inputs
outside the supported set (#37: an EUR dump, program `0004000000033600`,
refused as an unknown revision; #58: Korean).

| Issue | Report | Inventory | State | Remaining check |
| --- | --- | --- | --- | --- |
| #12, #35 | 0xC0000139 before the program starts | new | fixed (section 7: the runtime imported Vulkan 1.2 commands) | L6 audit of the package; retest by the reporters |
| #17 | remapped shoulder buttons do not use items | H1 | L2 pass: 24 shoulder/trigger assignments (`oot3d_native_a32_input_tests`) | L7 with the reporter's pad |
| #19 | erasing a file never completes | - | fixed in alpha.2 (`d1f57f9`), checked by hand | L5: erase a copied save from file selection |
| #20 | gyro yaw unresponsive (third-party Switch Pro pad through BetterJoy) | H2 | pitch fixed according to the reporter | L7 device checklist |
| #23 | access violation in `vulkan-1.dll` at repeated starts after an hour of play and a save (alpha.1c, RTX 3080 Ti) | new | the line validates its persisted pipeline cache (device identity, size, checksum, atomic write); S23 (2026-10-05): a Game Over save, then a fresh start on the same save data and cache, runs without fault through the title, the version choice and the file selection to the name entry (the restart's A presses choose the first slot, a new game in that save data). The earlier reading "stops at the version selection" was the frontend menus being invisible (D2, E9) | retest by the reporter |
| #25 | HUD and menus at 21:9 | E1 | pass (S03, S04 pages) | - |
| #27 | Forge does nothing from an external hard disk | new | not analysed | L6 installation on a second volume; L7 on a removable drive |
| #28 | the Normal / Master Quest choice is not visible in single-screen mode | E9 | fixed 2026-10-05: the choice (two cards, "Please select a game mode.") is drawn to the lower screen in game mode 1, which neither the TopScreen frontend relocation nor the OoT3D profile's lower-screen presentation covered; TopScreen 2.1.1 shows it on the upper screen. Now presented in both profiles; D-pad, circle pad, A/START and a touch or click on a card act natively (the native selector always accepted D-pad and circle pad; the first diagnosis read the version description after the choice as the choice). S28, S01 | retest by the reporter |
| #30, #55 | interpolated 60 FPS and uncapped stutter (RX 6600, 240 Hz, with and without VSync); Original 30 smooth | new (D1, D3) | the alpha.2c report shows 138 runtime shader compilations, 115 audio underruns and 61 % of the session inside present; the hybrid preparation removes waits for programs. S20 (real time, Kokiri walk, Enhanced, warm cache, 60 Hz display): VSync on 59.96 FPS, frame time p50 15.6 / p95 22.3 / p99 28.3 / max 43 ms, no underrun; VSync off 196 FPS, p99 10.6 ms, 1 underrun. The dropped-refresh counter is dominated by the stall after a state load (about 190 in every run) and is not windowed | L7 on a high-refresh display; occasional two-frame hitches with VSync on (p99 28 ms) to analyse |
| #31 | stutter while assets load from a hard disk | new | not analysed | latency injection for RomFS reads, or L7 on a hard disk |
| #32 | minimap markers misplaced | E2 | placement fixed with E1 | L5 S03 markers on fresh entry and after reload |
| #39 | ZL/ZR items, item numbers, map screen | R1, R2 | pages pass (S04) | L5 S04 assignment, use and one-frame taps |
| #40, #42 | Epona stamina over the timer; mounted B counter | E5 | pass (S05, 2026-10-05): race timer and clock above the carrots at 16:9 and 21:9, mounted B count separate from ZR, no TopScreen hook failure | - |
| #41 | Chamber of Sages grey instead of black | (`TRIAEVUM_ISSUES_40_43.md`) | pass (S19, 2026-10-05): the user's checkpoint and toon settings reproduce the look the user accepted on 2026-09-17 | - |
| #43 | iron boots hotkey repeats | E5 | pass (S06, 2026-10-05): holding the hotkey changes the equipment once and refreshes the player once | - |
| #45, #49 | Temple of Time softlock; crash after the revelation | I1-I4, D2 | pass under Original30 (S09) and interpolated (S09-revelation-interpolated, 2026-10-05: flashback over every column at 16:9 and 21:9; a savestate written mid-flashback restores and the sequence continues to Zelda's dialogue) | - |
| #46 | Game Over without choices | E6 | directional and A pass (S08) | touch input (the timeline carries touch) |
| #48 | crash when the title appears | C5 | no crash (S01); early Start opens the version choice (S01-title-early-start, references), which alpha.3b drew only to the lower screen | - |
| #51 | F1 enters fullscreen instead of opening the menu | E4 | L2: the window toggles from the key event (`three_ds_sdl_controller_tests`) | F1 tap in the product (L7, or a host-key input for L5) |
| #53 | Forge cannot write the module in a deep path | A2 | L2 | L6 installation into a padded root |
| #56 | HD texture packs do not reach the TopScreen HUD | new | not analysed | investigate; L5 with a pack that replaces one HUD texture |
