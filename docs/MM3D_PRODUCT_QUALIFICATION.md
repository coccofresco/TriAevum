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
  `resources/mm3d/project-restoration/layout/`, under the release-policy role `restoration_mm3d_asset`. This follows
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
