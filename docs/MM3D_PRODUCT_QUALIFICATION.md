# MM3D local product qualification boundary

This is a qualification ledger, not a supported-product declaration. MM remains
private/local; do not publish inputs, captures, prepared assets or packages.

## Installed product (FULL audit F07)

| Platform | MM installed/Forge status | Evidence boundary |
|---|---|---|
| Windows x64 | Not yet qualified | The local dual-title registry runtime and its tools-OFF build are not an approved whole-AOT MM title module. |
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

External MM-specific dekui/Restoration configuration (including
`presentation.json`) and damage overrides are **not** in that snapshot. Shared
graphics camera/FOV preferences remain part of the graphics owner above. The MM adapter supplies an explicit notice to the shared save-state
panel, save result, state metadata and settings-restore log. This is an omission
disclosure, not implementation of MM settings persistence. Ordinary state-only
load leaves current host settings in place. A malformed schema is rejected before
mutating settings owners; rejecting individual owner fields does not make the
whole settings application transactional.

The host state tests use synthetic memory, including an MM-tagged process
identity, foreign-title/source-snapshot rejection, corrupt/truncated/unsupported
containers and sentinel checks. These are not native MM cartridge-save round trips.
The window restore path resets title session and composition owners before resumed
submission, and resets renderer/history/UI/rumble owners. New title adapters remain
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

F07 and F15 remain **OPEN**. Beyond the absent approved MM recipe/module, Forge's
`forge-core/src/backup.rs` still declares `GAME = "oot3d"`; its existing backup
receipts cannot be described as title-isolated MM installed recovery. Do not
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

## Evidence

External private receipts: `W/analysis/parity/INTEG/final-product/`, with integration
summary in `W/analysis/parity/INTEG/RESULTS.md` and append-only `PROGRESS.md`.
Prior `ab74e246` captures and `b1e79383` offline receipts remain separate.
Consult the current results for which gates actually ran; this document does not
predeclare a pending test or capture successful.
