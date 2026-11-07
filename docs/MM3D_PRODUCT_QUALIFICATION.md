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

## Evidence

External private receipts: `W/analysis/parity/INTEG/final-product/`, with integration
summary in `W/analysis/parity/INTEG/RESULTS.md` and append-only `PROGRESS.md`.
Prior `ab74e246` captures and `b1e79383` offline receipts remain separate.
Consult the current results for which gates actually ran; this document does not
predeclare a pending test or capture successful.
