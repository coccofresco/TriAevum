# MM3D local product qualification boundary

This is a qualification ledger, not a supported-product declaration. MM remains
private/local; do not publish inputs, captures, prepared assets or packages.

The latest continuation is under **Recovery continuation** below. The fixed-cut
execution section is historical; its O2 pass does not qualify the newer runtime.

## Installed product (FULL audit F07)

| Platform | MM installed/Forge status | Evidence boundary |
|---|---|---|
| Windows x64 | Not yet qualified (installed product); per-title Forge in place, MM title module absent | Round-3 cut (2026-11-08): Forge is built per title (`forge-catalog` feature `title-mm3d`; OoT3D default unchanged). The MM3D Forge loads only `titles/mm3d/forge/supported_revisions.json` (recipe `mm3d-eur-local-6eec27b1`, the dump is the only input; Restoration romfs members are package resources, not user input) and rejects the OoT3D dump by name, and vice versa (real EUR dumps, both builds). `validate_title` is bound to the CATALOGS MM title-module contract through a FIXTURE only. Still blocking an installed run: (1) no MM whole-AOT title module / translator identity has been built (CATALOGS: disk plan pending owner); (2) the whole-AOT product dispatch cannot apply dekui/Restoration guest patches (`ImageMounted` throws "guest patch sites need the A32 registry dispatch"): `DispatchWholeAotProduct` has no native-function hook and whole functions do not stop at mid-function patch PCs, so the MM module must be translated with every guest-patch PC as an exit boundary before the product path can dispatch them like the registry build. Developer-registry evidence: rounds 2-3 state/settings/restart/rejection; round-3 cut O7 product-config smoke (developer tools OFF, registry build) ok. |
| Linux / handheld | Not yet qualified | Shared packaging/ELF infrastructure and OoT qualification do not qualify MM. |
| macOS | Not yet qualified | Shared bundle/privacy policy and synthetic tests are not an MM import/install/launch receipt. |
| Android | Not yet qualified | Runtime/mobile infrastructure is not an approved MM Forge package or device acceptance. |
| Switch | Not yet qualified | No MM installed artifact/device acceptance in this integration slice. |

`tools/triaevum_release/supported_revisions.json` does not yet provide an approved
MM recipe. Optional title vertex/pass packs and title-specific cache namespaces
do not supply a qualified whole-game shader corpus. MM's `ImageMounted` whole-AOT
patch boundary must be resolved and qualified, not bypassed merely to make a
product-info smoke pass. See `TRIAEVUM_TITLE_SHADER_PACKS.md` and the FULL F05
acceptance. A successful registry product smoke cannot close F05 or F07.

An eventual supported row needs private hashes for the actual packaged runtime,
module, recipe, resources and installation receipt; compiler-free import/install,
launch, input, saves, restart, invalid input and update/repair preservation on that
platform. Generic offline packaging tests qualify only their tested mechanisms.

## Saves/settings/recovery (F15)

The shared state+settings format owns renderer preferences, active TopScreen
settings, host controls and selected game language. Display/window/output/VSync,
monitor and refresh remain machine-owned. Language changes require a full restart.
Authored scene-environment files are not copied into states.

MM's mod settings (dekui's presentation settings, which include the Project
Restoration options and the damage multiplier) travel with "state + settings"
under a `title` block keyed by the title id (`mm3d`), exactly as OoT3D's TopScreen
settings do; they are applied through dekui's own parser and live apply. A block
for another title is kept out ("stored for another title, current kept"); a
damaged block fails without changing the live settings; a state without the block
keeps the current settings and says so. Ordinary state-only load leaves current
host and MM settings in place. A malformed schema is rejected before mutating
settings owners; rejecting individual owner fields does not make the whole
settings application transactional. Tests: `oot3d_mm3d_single_screen_tests`
(live dekui owner round trip, damaged text) and `triaevum_f1_settings_smoke`
(shared capture/apply; negative check: the old capture code fails it).

The host state tests use synthetic memory, including an MM-tagged process
identity, foreign-title/source-snapshot rejection, corrupt/truncated/unsupported
containers and sentinel checks. On the real MM binary (developer registry build,
c84c19b1), one run saved a state, then rejected an OoT3D state ("savestate code.bin
identity is incompatible"), a future container version ("savestate container header
is invalid") and a corrupted payload ("savestate payload checksum does not match"),
kept running and loaded its own state. These are not native MM cartridge-save round trips.
The window restore path resets title session and composition owners before resumed
submission, and resets renderer/history/UI/rumble owners. The MM title session's
`Reset()` also starts dekui's per-site host state (camera traces, menu, HUD and
banner animation) afresh, the same state a cold `--load-state` starts from;
settings and registered sites are kept. Project Restoration's guest calls
are host coroutines no state can carry: as OoT3D defers a save while a host-held
continuation is pending, the save waits while a guest call is in flight
(`SavestateCapturePending`), and restore drops the abandoned timeline's suspended
calls and pending site skips (`oot3d_mm3d_guest_calls_tests` TestResetOnRestore;
it fails with a no-op reset). The light arena keeps no host state to reset: its
overflow mapping is a guest memory region restored with the state and revalidated
by region and magic before use. New title adapters remain
responsible for clearing their own lifecycle state in `Reset()`.

Forge's Rust `forge-core` owns data inventory, backup/restore, repair and interrupted
activation recovery (`inventory`, `backup`, `restore`, `data_actions`, `state`).
Its disposable fixture tests must not be represented as MM installed-product
qualification. Real MM native save import/export, title-isolated installed paths,
upgrade/repair preservation and restart still require a qualified installation.
Never test repair against the operator's original save directory.

## Crash provenance and redaction

Keep failed attempts alongside successes. Record runtime source revision and dirty
diff, exact executable/modules, build configuration/NRI patches, effective config,
resource/catalog/overlay identities, state/input hashes, exit/watchdog result and
run directory. A documentation-only later commit does not change the runtime
revision written on a receipt. Never relabel older captures as newer binaries.

Crash dumps, logs and support bundles may contain local paths, configuration,
user data or guest memory. Keep originals private; use the Forge diagnostics
redaction/inventory policy for a reviewed support bundle, not raw-dump publication.
Backups exclude crash diagnostics. Set `TRIAEVUM_CRASH_DIALOG=0` for unattended
qualification, and run games only through the coordinated safe/watch scripts.
The historical product `bad_alloc` remains unexplained; later successful execution
means **not reproduced**, not fixed.

## Coordinator-approved fixed cut (OVERLAY excluded)

The approved input refs for this cut are HOOKS `88495d24`, COMPOSITION
`b053e8be`, LANGAUDIO `187f815e`, and CATALOGS `eff80eb6`. These are fixed
committed inputs, not a promise to chase subsequent producer changes. The
CATALOGS native-lighting/GGX family checkpoint is included as requested; its
unfinished startup/Forge/publication work remains explicitly unqualified.

Both earlier OVERLAY merges and the dependent replacement-label HUD bridge are
reverted for this cut. Their ancestry is retained for audit, not runtime inclusion.
Native MM HUD, HOOKS HUD classification and COMPOSITION FXAA ownership remain;
the replacement-label/emission hooks, preferences and acceptance driver do not.
OVERLAY requires a separately approved later cut. The unmodified v10 generated
registry is sufficient for the included hooks; no OVERLAY continuation is required.

The combined MM batch and product smoke must use the same final tools-OFF
executable/modules after all offline builds. Product camera framing comes from
the supplied states, without developer-only freecam overrides. State/settings
and graphics-schedule CLI actions remain available in that binary. OoT O2 uses
its separately built OoT-only binary from the same frozen runtime source.

## Fixed-cut execution receipt (2026-11-07)

Frozen runtime source **3a1fb05f4e9f69a9dfb4cb63c33e1b716265d609**.
Later storage-test/documentation commits do not alter the captured runtime.
The product executable is dual-title, registry v10, developer tools OFF;
SHA-256 `8608795eae572f6ee9cdbfc2e0567d372c6fe3747474712aea7d0cfe46df9a60`.
OoT-only O2 executable SHA-256
`ad20408cf024ac67d165ae05e303a66c757f1f0b9308ffb3cba1f76579996535`.

- O1/O6: 46 offline invocations passed on the frozen source, including actual
  full/environment/grass F1 widgets. O5: six legacy packs unchanged, new
  104-program native-lighting family regenerated and checked.
- O2: one execution, all three Kokiri BMP hashes equal `cap-p13.sha256`.
- Product smoke: **FAILED before the first frame**. The prepared LANGAUDIO
  O3RO changes the RomFS service view; the supplied legacy state has open
  handles with offset 0 / size 659384080. The restore compatibility check
  correctly refuses the new view. No smoke retry and no guard bypass.
- After offline decoding all five states, the separate planned MM batch used
  their original `mods/dekui-romfs` directory overlay, not the new O3RO/message
  bundle. Noon/night/field/Inn/Woodfall all exited 0 on the same product binary.
  These runs do not retroactively pass the consumed Authentic smoke or qualify
  the new language preparation.
- Field: one save+settings and two in-run loads (with settings, then state-only)
  succeeded; current display and language were retained, the MM settings omission
  was disclosed. Grass was restored Off with settings and remained On after the
  later state-only load. This is bounded live state/settings proof, not all reset
  identities or native cartridge-save gameplay qualification.
- Additional offline CTR filesystem test: 79,384 opaque bytes from a read-only
  real local save pass through SVC/IPC, rooted storage, flush and host restore.
  Only disposable output is written. A mismatched RomFS size is rejected without
  damaging the live handle. This is filesystem proof, not a game save/load cycle.

At this fixed cut, F07 and F15 remained **OPEN**. Beyond the absent approved MM
recipe/module, Forge's backup identity was hardcoded to `oot3d`; those historical
backup receipts cannot be described as title-isolated MM installed recovery. Do not
relabel MM bytes as an OoT backup or relax supported-recipe/restore checks.
Required follow-up is a title-bound Forge backup contract together with the
qualified MM installation, native game save/restart and supported-platform
matrix. Neither the current registry run nor synthetic recovery tests supplies
that dependency. Historical bad_alloc remains unresolved; this smoke failed for
an independently diagnosed RomFS mismatch instead.

No additional game acceptance is implied by this document. The reserved seven
executions were consumed: six succeeded, one smoke failed. Full button OVERLAY,
newer producer tips, full shader corpus, physical devices/platforms and complete
rendering parity are not qualified by this cut.

## Recovery continuation (2026-11-07)

From handoff `6cdd4d9e`, `94dded39` implements the title-bound backup contract in
[TRIAEVUM_BACKUP_TITLE_IDENTITY.md](TRIAEVUM_BACKUP_TITLE_IDENTITY.md). Both foreign
restore directions and relabeled wrong-revision archives fail before writes;
non-OoT round-trip and interrupted rollback preserve disposable save bytes.
61 core tests and one CLI test pass. The optional real-save fixture remains
opaque storage proof, not native gameplay or an approved MM installation.
The backup identity implementation subgap is closed; installed recovery is not.

OOT3DPREP `e490e335` / `38a4b57a` are merged by `d86e49ee`. Both tools-OFF builds
succeed with the private patched NRI; MM remains dual-title/registry v10. The
five affected offline suites, private NRI gate and OoT vertex provenance check
pass. All shared renderer pack bytes remain unchanged from `3a1fb05f`.

Exactly two separately requested guarded OoT tests ran, once each:

| Acceptance | Actual result |
|---|---|
| Seeded O2 identity | **FAIL**: exit 0, frame 300 matches `cap-p13.sha256`, frames 100/200 differ. Startup 0.659 s. |
| Empty-application-cache startup | **PASS**: exit 0, startup 0.860 s, initial pipeline-cache bytes 0; all three frame hashes match. Driver cache retained. |

Both report 55 parts, zero preparation failures and zero unavailable uber
categories. The cold acceptance is **not** an O2 retry or replacement pass.
Lossless comparison to its matching captures isolates seven differing pixels
in each failed seeded frame, red-channel delta 8 (one RGB5 quantization step).
Prolonged uber use correlates with shader-cache write latency: seeded 29.139 s
versus cold 0.130 s, uber draws 26019 versus 3569. The causal shader/site and
fix remain unproved. No cache-policy workaround, runtime edit, reference hash
change or extra confirmation run was made. O2 remains an open regression.

All five platform rows above remain **not yet qualified**, not supported or
proven intrinsically unsupported. The approved MM recipe/whole-AOT module and
`ImageMounted` patch-dispatch contract remain unresolved. Native game
save/restart, installed import/export and repair/update preservation, and the
full lifecycle reset matrix remain open. No MM game was launched in this
continuation. Neither historical O2 nor producer-lane evidence replaces the
failed post-merge seeded INTEG acceptance. Complete parity is not met.

Private continuation receipts are in
`W/analysis/parity/INTEG/recovery-continuation/`, particularly
`acceptance-summary.json`, `offline-gates.json`, both `*-inputs.json` receipts,
`shared-inputs.json` and `seeded-vs-matching-cold.json`. Captured runtime source
is `d86e49eebf60b2788878c218272190d921bb947c`; later documentation does not change it.

## Evidence

External private receipts: `W/analysis/parity/INTEG/final-product/`, with integration
summary in `W/analysis/parity/INTEG/RESULTS.md` and append-only `PROGRESS.md`.
Prior `ab74e246` captures and `b1e79383` offline receipts remain separate.
Consult the current results for which gates actually ran; this document does not
predeclare a pending test or capture successful.

## Round-3 integration cut (2026-11-08)

Merged lanes: hooks 342f1f25, composition 59b411b4, overlay 6514425c (after reverting the earlier exclusion
eb75912d), langaudio 4ea18c7a, catalogs c5f49724, dl f8254087. Resolution rules: union of lane intent; OVERLAY's
label architecture with the Hide-HUD contract ported; one rumble owner (HOOKS `Mm3dRumble::Source`). Dual-title tree
uses registry `a32_generated_overlay`. Gates: affected suites 35/35 + O1 foundation (472 pass, 1 skipped), nri patch
set, F1 full / --scene-environment / --grass-masks (O6) exit 0; O5 six packs regenerated byte-identical; O2 on the
OoT-only tree (seeded, 3/3 cap-p13, reproducible=1, uber_draws=0); MM developer capture batch (5 cases) and O7
product-config MM smoke ok. Evidence: `W/analysis/parity/INTEG/r3cut/`.

## Per-title product chain, session round 2 (2026-11-08)

- Whole-AOT product dispatch (892d6656): `DispatchWholeAotProduct` consults the native-function callback before the
  compiled title module; the MM3D `ImageMounted` product-mode refusal is replaced by Restoration data patches, guard
  verification and a check that every enabled guest-patch site (PC, PC+4, declared targets) is a compiled-module
  function entry. A module without them is refused with a message naming the first missing PC. The developer
  registry path is unchanged. Proven by unit tests plus a recorded mutation negative, and by product-mode syntax
  checks of both translation units. It cannot run until an MM title module exists (CATALOGS).
- Restoration package members (905e7f9f) sit at `resources/mm3d/project-restoration/layout/...`. The `romfs`
  component is dropped because the release policy forbids it, and a test checks this against `release_policy.json`.
- Runtime title identity: `--product-info` publishes `titles` (`["oot3d"]` for an OoT-only build,
  `["oot3d","mm3d"]` for a dual build). The per-title Forge refuses a runtime without its title and names it. A
  runtime without the field is treated as OoT3D-only, so the OoT3D Forge's behaviour is unchanged.

F07 Windows row: **NOT QUALIFIED**. No installed-product run was made, because no MM whole-AOT title module or
translator identity exists to install.

## Per-title Forge, session round 3 (2026-11-08)

- Update/state verification no longer assumes OoT3D. `package_inventory` required `recipes/oot3d.json` in every
  update package, and installation state reported a missing `recipes/oot3d.json`. Both now use the Forge title's
  packaged recipes (`recipes/oot3d.json` or `recipes/mm3d.json`). Without this fix, every MM3D Forge update would
  have been refused. The OoT3D value is unchanged, so the OoT3D Forge's behaviour and messages are identical.
- forge-core fixtures (recognition, inventory activation paths, update roles) follow the active title, so the MM
  Forge runs the same suites as the OoT3D Forge. The pre-fix MM run is the recorded negative (14 update failures:
  "The package has no recipes/oot3d.json").
- validate_title is bound to the CATALOGS artifact contract through a FIXTURE module
  (`per_title_forge.rs::mm3d_title_module_contract_binds_through_validate_title`). The real MM module is still not
  built: CATALOGS is blocked on the object-cache disk plan, which is an owner question.

F07 Windows row: still **NOT QUALIFIED**. There is no MM title module to install, so no installed run was made.

## Round 4 (2026-11-08): Restoration archives, MM3D shader packs, light-overflow restore

- **Restoration layout archives (Q-INTEG-1).** The LOCAL MM3D package bundles Project Restoration's modified
  Nintendo layout archives `Joker.Main.New.gar` and `Joker.Main.Play.gar` as-is, at
  `resources/mm3d/project-restoration/layout/`, under the release-policy role `restoration_layout_asset`. This follows
  the TopScreen `topscreen_singlescreen_asset` precedent (ctxb/zar): `.gar` is a forbidden extension except for that
  role, the role lists exactly those two paths, and their SHA-256 values are pinned (`pinned_role_sha256`). The
  Forge-side mirror (`forge-core` `package_inventory.rs`) and the Restoration member test (`forge-assets`
  `mm3d_ui.rs`) follow the policy. The members stay separate files, so a delta form (a patch against the
  user's own original archive) remains possible without changing the role's consumers. **The public shipping form
  is an owner decision, deferred** until MM3D publication is considered; nothing here publishes or exports it.
  `Joker.mfpj` is not a Nintendo archive and stays an ordinary `title_neutral_resource`.
- **MM3D prepared shaders (Q-CAT-3, parity with OoT3D).** `Mm3dTitleSession::PreparedShaders()` is true exactly
  when MM3D's built-in packs, `titles/mm3d/shaders/mm3d_native_vertex.spvpack` and
  `titles/mm3d/shaders/mm3d_native_fragment.spvpack`, both exist at configure time. They are then embedded
  (`triaevum_add_mm3d_builtin_shader_packs`, `TRIAEVUM_MM3D_BUILTIN_SHADER_PACKS`). There is no product or developer
  switch and no runtime pack-identity switch. The window session installs the session's packs before the renderer
  starts: the vertex family goes through `InstallTitleBuiltInVertexPack`, after OoT3D's embedded programs, and becomes
  the startup family returned by `AdditionalTitleVertexArtifacts`; the fragment families go through
  `InstallTitleFragmentPack`. The pipeline cache namespace stays `mm3d`. OoT3D installs nothing, so its families and
  files are unchanged. The packs are produced by CATALOGS; until they exist, MM3D keeps prepared=0 (complete mode).
- **Light-arena overflow across a restore.** `EnsureMm3dLightOverflowMapping`, shared by the session and
  `mm3d_light_overflow_restore_tests`, revalidates the exact region and its identity header after a savestate
  restore. It runs on the real `NativeA32Memory` Capture/RestoreState. The overflow record is preserved. A state from
  before the mapping existed is refused until an allocation re-creates the mapping. Corrupt, executable and partial
  foreign regions are refused.

## Round-4 final integration cut (2026-11-08)
- Base 1fb565f9. Real merges: langaudio 28c47234 (clean), catalogs 75e1cc84 (clean; vertex-family regenerator,
  no built-in MM packs yet), hooks 542c1233, composition e5418a69, dl 9ad748e7 (dl lane = TriAevum-parity).
- Conflicts resolved keeping both intents: mm3d_pica_composition.{cpp,h} (integ primitive/quad-span stats and quad
  index + hooks P17b quad-object census + composition room-slot/direct-shadow stats); title session Reset and dekui
  core_tests keep integ's superset hunks; scene_emitters.py keeps forge_profile/unreferencedRooms with dl's
  generatorRevision 3.
- 46f546ff fixes a boundary violation from cd495617 (a title token in a shared renderer header comment), caught by
  verify_renderer_3ds_cross_title_boundary in the first cut build.
- Affected suites (dual tree): 37/37 exit 0, plus the boundary check. Python: scene_emitters 11 passed; tools/mm3d 9 OK.
  Forge sources are unchanged by the cut; the full Forge suites stay as evidenced at 1fb565f9 (245/245 per title).
- O2 on the OoT-only tree at 46f546ff (TriAevum.exe sha256 43b7317f...): 3/3 frames match cap-p13, reproducible=1,
  uber_draws=0, watch_exit=0. Evidence: analysis/parity/INTEG/r5/o2/.
- Still open: MM built-in packs from CATALOGS (prepared=1 then the uber requalification); F07 blocked by the
  owner disk plan; the public form of the Restoration .gar members is owner-deferred.

## Registry rule (coordinator, 2026-11-08, after the round-4 F14 failure)

Every MM3D build whose source contains the integration cut (the dekui/Restoration guest patch sites of
`titles/mm3d/dekui/core_test_sites.h`, among them the button-emission boundaries 0x134DE4/0x134DE8) is configured
with the A32 registry `a32_generated_overlay`. `a32_generated_v10` and `a32_generated_v11` predate those
boundaries: a game built with them dies at startup. They are only for trees without the cut.

Configure enforces the rule. When the build contains MM3D and the site table exists,
`titles/mm3d/a32/check_registry_boundaries.py` checks that every site PC and PC+4 is a block entry of the chosen
`registry.cpp`, and configure stops with "MM3D registry rule" otherwise. On the real registries: overlay has all 299
boundaries (exit 0); v10 and v11 each lack 0x00134DE4 and 0x00134DE8 (exit 1). A configure of the dual tree with
v10 fails (the recorded negative), and the same configure with overlay succeeds. INTEG's own configure script now
defaults to the overlay registry. Evidence: `W/analysis/parity/INTEG/r6/registry-rule-*.log`,
`configure-v10-negative.log`, `configure-overlay.log`.

## MM3D built-in shader packs merged (2026-11-08, integ round 5)

CATALOGS 13bfeaca merged into the integration line (a real merge). The dual tree compiles
`titles/mm3d/shaders/mm3d_builtin_shader_packs.cpp` with `TRIAEVUM_MM3D_BUILTIN_SHADER_PACKS=1` and the real pack
paths. Both packs are embedded byte for byte in `oot3d_native_game.exe`: vertex 1580888 bytes (sha 6a0ded23),
fragment 16 bytes (an empty pack; the fragment families are shared). A one-byte mutation of the vertex pack is not
found (the negative). Built-in pack tests (present/absent, with `--negative` exit 1), title_shader_preparation and
renderer_spirv_pack pass. The live prepared=1 line is CATALOGS' single hybrid capture, not repeated here. Evidence:
`W/analysis/parity/INTEG/r6/embedded-packs.log`, `r6/logs/`.

## Per-title release preparation (2026-11-08, integ round 5)

`prepare_release.py --title mm3d` prepares the MM3D Forge release. It builds the Rust Forge with
`forge-desktop/title-mm3d,forge-cli/title-mm3d`, packages `titles/mm3d/forge/supported_revisions.json` as
`recipes/mm3d.json` (recipe `mm3d-eur-local-6eec27b1`), and requires `--restoration-resources`. It bundles
Project Restoration's members under `resources/mm3d/project-restoration/layout/`, each checked against its pinned
fingerprint. The two `.gar` archives use the role `restoration_layout_asset`, and `Joker.mfpj` is a
`title_neutral_resource`. The default `--title oot3d` keeps OoT3D's release unchanged: its recipe file, recipe path,
default recipe, and no Forge feature. The MM3D release still needs the MM whole-AOT title module from the module
window. The installed run (F07) has not been made.

## Round-5 final integration cut (2026-11-08)

Merges onto b0191231: 73ae8cb6 hooks 4648c4bd (one conflict in `titles/mm3d/adapter/mm3d_pica_composition.cpp`,
resolved by keeping both sides: the quad/primitive flush and the layout-batch carry), bef79f0d composition 049bd3ce
(include-list conflict in `mm3d_native_lighting_tests.cpp`, union), 7bc7fb60 catalogs 44688869, b8e9a214 langaudio
ff5e7a8f, deeeb68e parity/dl 1da09ade. The dual tree is configured with registry `a32_generated_overlay`, and the
build exits 0. 40/40 affected suites exit 0. The negative (built-in packs `--negative`) exits 1. The O2 ran once on the
OoT-only tree at deeeb68e (TriAevum.exe sha256 c62d0264...) and passed: 3/3 cap-p13 frames identical, reproducible=1,
uber_draws=0, watch_exit=0. Evidence: `W/analysis/parity/INTEG/r7/`.

## Round-6 per-cut qualification of 89573e5d (2026-11-08)

Correction: 89573e5d (merge of catalogs 0d53938e) was described in round 5 as a test-only merge covered by the
deeeb68e O2. It is not: it also brings c6dd094a (offline variant-replay tool), 5d8b549a and 88b48850 (shared
`whole_aot_program.py`/`whole_aot_cpp.py`: BLX-to-Thumb calls, epilogue observers without Pc+4, MM guest-patch
exports), b6f242e8 and efc72b6c (developer-only shadow-caster view-lag probe in `gfx_vulkan_pica_draw.cpp`,
`#if TRIAEVUM_RENDERER_DEVELOPER_TOOLS`). Requalified at that tip: dual tree (registry `a32_generated_overlay`) and the
OoT-only tree rebuilt, both exit 0; 41/41 affected suites exit 0, including `oot3d_mm3d_guest_patches_tests` and
`oot3d_graphics_foundation_tests`; the negative (built-in packs `--negative`) exits 1; whole-AOT generator Python tests
59 passed. ONE O2 on the OoT-only tree (TriAevum.exe sha256 1af9f7fd...): 3/3 cap-p13 frames identical,
reproducible=1, uber_draws=0, watch_exit=0. Later commits of this round touch only the Forge Python and title data
(per-title whole-AOT frontend), not the game executable. Evidence: `W/analysis/parity/INTEG/r10/`.

## Per-title whole-AOT source frontend (2026-11-08, integ round 6)

The Forge's whole-AOT source frontend (`whole_aot_source_backend.load_default_frontend`) is selected by title. The
prepared recipe id chooses it (`forge.recipe_title`): a recipe listed in MM3D's `recipes/mm3d.json` or
`titles/mm3d/forge/supported_revisions.json` selects MM3D; every other recipe keeps OoT3D's frontend unchanged; a recipe
listed by two titles is refused. MM3D's inputs are title facts in `titles/mm3d/a32/whole_aot_frontend.json` (inventory,
guest-patch supplemental entries, boundary audit, product manifest, selection, base 0x100000); every role must be a file
under `titles/mm3d`. The MM3D product manifest and selection are the zero-residual product contract (G2) and do not
exist yet, so the MM3D frontend is refused with their names until G2 lands. Tests: `test_whole_aot_title_frontend.py`
(10 passed); the negative (the OoT hardwire restored) fails 3 tests. Evidence: `W/analysis/parity/INTEG/r10/forge/`.

## Module window part 2 (2026-11-08, integ round 7)

Merged catalogs a063f16d (F11 probe moved out of the shader decision-policy inputs) and branding 81eb971a (product
executable names: OoT3D `Echoes_of_Time.exe`, MM3D `Moonfall_Curse.exe`; per-title update helper; MM window icon).
Both trees rebuilt (exit 0); 42/42 affected C++ suites, the whole-AOT/release Python suites and the Forge suites of both
titles pass. Per-cut O2 (game sources changed): OoT-only TriAevum.exe bf2cb10e, 3/3 cap-p13 frames identical,
reproducible=1, uber_draws=0. Defects found on the MM product path and fixed: forge-cli fixtures followed OoT3D only;
the MM package built the update helper without its title feature (OoT icon); the process library did not own its
generated-registry link (MM `oot3d_game_module` link failure); the Forge GUI probe's sample path tripped the release
build-path audit; the reviewed extra-input coverage (OoT3D's) was applied to MM3D. The MM product runtime (developer
tools OFF, direct plugin, registry `a32_generated_overlay`, `TRIAEVUM_PRODUCT_TITLE=mm3d`) builds and embeds the MM
icon; the MM Forge and update helper embed the steel-mask icon (offline icon receipt).

F07 Windows row: still **NOT QUALIFIED**. `prepare_release` refuses the MM shader corpus at this tree: two
decision-policy inputs (`graphics_settings.h`, `gfx_vulkan_pica_draw.cpp`, composition 049bd3ce: the projected-shadow
omission condition moved into `OmitsNativeProjectedShadowRole`) differ from the corpus recording source. The corpus was
requalified at the CATALOGS tip, which does not carry 049bd3ce. The gate requires fresh variant recordings; no package
was produced and no installed run was made. Evidence: `W/analysis/parity/INTEG/module-window/out-r13/`.

## Module window part 3 (2026-11-08, integ round 8)

The corpus re-recorded by CATALOGS at 35fe6696 (pack ddef4aee, qualification c347d0ee) qualifies against this tree
(check exit 0, SHA256SUMS OK); window preflight exit 0. OoT-only TriAevum.exe at the tip is up to date and identical to
the round-7 O2 binary bf2cb10e, so no new per-cut O2 was due (no C++ changed after it).

The MM package, install, status and bad-input steps now pass. Defects found on the MM install path and fixed:
- The precompiled release audit read `recipes/oot3d.json`; it now reads the release's single declared revision recipe
  (0b52b8c1).
- Forge asked the runtime for the ROM languages without the code identity, so the runtime read the MM3D RomFS as
  OoT3D's. Installation now passes the verified code.bin SHA-256 (ede9ba7e).
- The offline interior-sun prebuild (an OoT3D title tool) ran for MM3D and failed. It is now a per-title Forge fact, and
  MM3D records it as not applicable (94bdc772).

Package facts:
- 96 files. The catalog is re-laid with abi 2 and translator dd2ff99a; plugin 3e6ebcd0; runtime cf127a72.
- `validate_title` exits 0, and the tampered-DLL negative exits 1.
- The MM product-contract audit reports 18416 compiled functions, 0 host boundaries and 0 residual entries (exit 0). The
  OoT-contract negative exits 1.
- The Restoration members are bundled.

Installed layout:
- `Moonfall_Curse.exe` and `data/Moonfall_Curse.launch.json`. There is no root `TriAevum.exe` and no leftover
  `runtime/TriAevum.exe`.
- Icon receipt of the installed binaries: 9/9 MM images, 0/9 OoT images. The update helper and the Forge GUI carry the
  steel mask. The CLI `triaevum-forge.exe` has no icon in either title.

F07 Windows row: still **NOT QUALIFIED**. The one granted installed run exited 2 within one second, before any guest
execution. The cause was diagnosed offline and lies in the run harness, not the product: the private lab run harness (installed mode)
put `--screenshot-rgb8` before `--launch-profile`. The runtime only loads a profile given as argv[1], so it rejected
`--launch-profile` as an unknown option. The harness is fixed, and its dry-run receipt shows argv[1] =
`--launch-profile`. Under the run budget, no retry or diagnostic run was made, so no realtime product evidence exists yet.
Evidence: `W/analysis/parity/INTEG/module-window/out-r14/`.

## Module window part 4 (2026-11-08, round 9, INTEG): last granted F07 attempt

Preconditions (coordinator Q-INTEG-R8-1), all met before the run:
- The launch profile is a per-title fact: `ForgeTitle.topscreen_ui` (true for OoT3D, false for MM3D, whose per-game mod
  dekui is in the product code). The MM profile carries no `--ui-profile topscreen` or `--topscreen-config`; the runtime
  refuses that profile for any adapter but OoT3D EUR, so the r14 profile could not have started either. The OoT3D
  profile bytes are identical before/after (bd3cc9d6). Every option of the installed MM profile is parsed by the runtime.
- Forge recognition passes the verified code.bin identity to the language probe; the MM preview reports
  `language_error: null` with five languages.
- The isolated release Python suite (684 tests, `python -I` with the repository root on `sys.path`) exits 0. The publisher
  policy names no private title: the Restoration layout archives use the title-neutral role `restoration_layout_asset`
  (`resources/*/project-restoration/layout/*.gar`, still pinned to their two SHA-256).
- Package, install, status, badinput, icon (9/9) and dry/precondition receipts pass; no runtime C++ changed since
  35fe6696 at that point.

The run (out-r15, Moonfall_Curse.exe cae444f4): exit 1 after one second, before guest execution, with
`TRIAEVUM_FATAL the title module does not start a block at the object bank sites ...`. Cause (offline): the product build
required OoT3D EUR object-bank extension and SingleScreen rain sites to be block starts of every title's whole-AOT module.
Fixed in 3a5bad71: those sites are registered and required only for the OoT3D adapter (as the block-entry hooks already
were). Per-cut O2 of the OoT-only runtime (27f0dcf7): PASS 3/3. The MM product was rebuilt, re-packaged and installed
(out-r16: Moonfall_Curse.exe 2a470979, status ready, badinput refused, icon 9/9, profile without TopScreen options).

F07 Windows row: still **NOT QUALIFIED**. Under the run budget there was no retry; the next installed run is an owner item.
No realtime product evidence (F14) exists yet. Evidence: `W/analysis/parity/INTEG/module-window/out-r15/`, `out-r16/`,
`W/analysis/parity/INTEG/r15/`.

## Module window part 5 (2026-11-08, round 10, INTEG): startup self-check, then the granted F07 run

- Mechanism: the runtime's `--startup-check` (f17b8a83, d89f3348). It runs every startup requirement of the build on the mounted image, up to the host context, with no window or GPU: title module ABI v2, the product block-start sites (TopScreen and object bank, both gated per title), the title session's ImageMounted checks including MM3D guest-patch boundaries, and the crash diagnostics. Then it exits 0.
  - On the installed out-r17 package it found a real defect: the module lacked 0x00180B6C (photo camera +4), 0x001F9E94 (roll_dash.input +4) and 0x00231E9C (roll_dash.speed +4).
  - Fix 3b4d72b6: the product needs Pc+4 only where a handler returns there (per-site title fact HandlerReturnsNext=false; registry boundaries unchanged). Unit test positive exit 0, mutant negative exit 1.
  - Re-packaged and installed as out-r18 (Moonfall_Curse.exe de547284). Self-check positive exit 0 ("341 of 341 guest-patch sites enabled", whole_aot_available=1). Negatives: wrong (OoT3D) module exit 1; --ui-profile topscreen exit 1.
- OoT3D: the OoT-only exe changed 27f0dcf7 -> 16289971, so the per-cut O2 ran: PASS 3/3 cap-p13, reproducible=1, uber_draws=0.
- The F07 run (out-r18, the one granted run) FAILED: A exited after about 100 s with lab-harness result=stall, exit 12. game.err.log shows the product start chain passed: adapter mm3d, "MM3D product: 341 of 341 guest-patch site(s) enabled", Vulkan/NRI up, the native program preparation started (165 parts). Then: TRIAEVUM_FATAL "native A32 process faulted: native A32 dispatch exited unexpectedly: kind=6 pc=0x00100085 detail=1463897940". kind=6 is ExitKind::MissingBlock and detail 0x57414F54 ('WAOT') is DispatchWholeAotProduct's !handled exit. The first guest code after the process entry branches to 0x00100084 in Thumb state (pc bit 0 set). The MM whole-AOT title module (plugin 3e6ebcd0, kEntryPoints 238478, generation 2020030e) has no entry there and the product build has no interpreter fallback; the developer build's registry interprets it. Second defect: after the fatal the process did not exit and was killed by the stall watchdog (100 s). The --startup-check cannot see this because it is coverage of executed code, not a startup site. B was not started, so no runtime.json, no realtime fields and no settings round trip. No retry and no diagnostic run (run budget).

F07 Windows row: still **NOT QUALIFIED** (owner item). The fix needs a title module that covers the Thumb code reached at boot (generator/catalog work), plus the fatal-exit hang. No F14 realtime product evidence. Evidence: `W/analysis/parity/INTEG/module-window/out-r17/`, `out-r18/`, `W/analysis/parity/INTEG/r16/`.

## Module window part 6 (2026-11-08, round 11, INTEG): G3 module, fatal exit, F07 out-r19

- Fatal path (Q-INTEG-R10-2): an error exit now abandons the renderer's background startup preparation
  before teardown (`include/fast/backends/gfx_startup_preparation_abandon.h`, 8ff96f06 + 327bb3f1; outside
  the shader-corpus policy inputs, so corpus c347d0ee stays qualified). Unit contracts 5-6 of
  pica_preparation_queue_tests pass; the mutant (abandon ignored) fails with prepared=2000/2000, join 10.4 s.
- Merged: CATALOGS G3 (757f90ae: Thumb decode/lowering, zero residual, entry reachability) and LINUX
  (c9e3880e). Merge fix 67e0f662: a sealed Linux payload (AppImage/Flatpak) runs and profiles the runtime
  under its packaged name. Dev link fix 93a63166.
- OoT3D: generated program identical to 08696784 and C++ shards identical to 8b819acc (G3 is a no-op for
  OoT3D); per-cut O2 PASS 3/3 (OoT-only exe 37be366a at 93a63166).
- Package out-r19 (Moonfall_Curse.exe 0d9c257a, plugin 5840a572, generation 1cff87cb): preflight,
  package, install, status, badinput exit 0. The module table holds {0x00100085, Execute_thumb_00100084}.
  --startup-check exit 0 (module_entry_points=241676), negatives exit 1.
- F07 run out-r19 (the one granted run): FAILED, but later than out-r18. Thumb boot passed. After
  145.8 s, just after the background preparation ended (165 parts, no PICA draw yet), the guest faulted:
  MemoryFault (kind=4) at pc 0x0015127C, which is `ldr r0,[r0,#0x150]` in FUN_00151270 (192 ARM callers),
  read address 0x150, so r0 = NULL at entry. The process then EXITED by itself 3 s after the fatal line
  (lab-harness result=exit, exit 1; no watchdog kill). No runtime.json and no realtime evidence. Offline,
  we cannot tell which caller passed NULL: there is no register/LR trace in the product fault report.

F07 Windows row: still **NOT QUALIFIED** (owner item). The fatal path is fixed (the process exits).
Next: put the faulting thread's LR and r0-r3 in the product fault line (a runtime diagnostic change) and
find the NULL source offline, comparing against the interpreter or a donor. Then the run needs a new grant.

## Release cut part C (2026-11-09, integ round 12)

The cut merges the CATALOGS module window 4 (716b0ba6, fast-forward), the release fix branches fix/build-gate
(e20d0b63) and fix/privacy-tests (61a0cab2), and the Linux portability fix 13a9adf1. The public-tree privacy sweep
exits 0 on the merged tree. Dual developer build, C++ suites (49), Python suites and cargo Forge tests for both
titles pass. OoT3D: generated program identical to 08696784 and C++ shards identical to 53a2904f; the OoT-only
executable of the cut passes the per-cut O2 (3/3 cap-p13, reproducible=1, uber_draws=0). The MM product runtime
(registry `a32_generated_overlay_r18`, developer tools OFF, direct plugin, product mode) builds. The window-4
module table holds the S3 sites 5/6/7 and their +4 boundaries and the Thumb entries.

F07 Windows row: still **NOT QUALIFIED**, and no installed run was made at this cut. The corpus gate refuses
the MM shader corpus (qualification c347d0ee): the privacy sweep replaced a local path in a comment of
`azahar_texture_pack_import.h`, which is a decision-policy input, so the recordings count as stale. The rule says
the sessions must be recorded again, so the package and the run wait for that decision.

## Restoration layout produced at install (C-DISTRO-1, coordinator D25)

The Moonfall Curse package no longer ships `Joker.Main.New.gar`, `Joker.Main.Play.gar` or `Joker.mfpj` (the former
role `restoration_layout_asset` is removed). It ships `resources/mm3d/project-restoration/layout.recipe.json`
(`triaevum_restoration_layout_v1`), seven BPS patches under `layout/patches/` and the seven Restoration-original
members under `layout/members/` (D24 Q1 default; see `LICENSES/Project-Restoration.md`). The title-mm3d Forge
(`forge-assets` feature `title-mm3d`, module `mm3d_restoration_layout`) rebuilds the three files from the user's dump:
8 members by reference to the dump's `layout/Joker.Main.Play.gar`, 6 modified members and `Joker.mfpj` by BPS patch
(source, target and patch CRC32 checked), then emits the GAR v2 archives in their canonical layout. The result feeds the
unchanged `mm3d_ui::prepare` fingerprint check (`input_sha256` 3b5918df / 5b143a1e / ab79c651) and transform.

- Release policy: roles `restoration_layout_recipe`, `restoration_patch` (`.bps` exception) and
  `restoration_original_member` (`.ctxb` exception), each pinned by SHA-256; `.gar` has no role exception any more
  and `.mfpj` is a forbidden extension, so no `.gar`/`.mfpj` can enter a package (planted negatives in
  `test_release_audit.py`). `package_inventory.rs` mirrors the lists.
- Generator: `tools/mm3d/restoration_layout.py generate|check|members` (offline, `python -I`, reads the user's dump; never
  shipped). `check` reconstructs the three files byte-identical; `members` cuts the original members from the
  Restoration release at packaging time (`title_release.restoration_members`).
- Acceptance: the prepared MM UI layer of the title-mm3d Forge equals, file for file, the layer of the cut-14b Forge
  that read the whole files (only the receipt's generator identity differs, so an existing install re-prepares once
  with identical bytes). No recording is invalidated and no game run was needed.
