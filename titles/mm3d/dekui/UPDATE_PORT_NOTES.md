# dekui 6e36dfb4 port status, 2026-10-25

This is a partial, tested port. The remaining class (a) work is stopped at the
unproved PromptText language-archive translation, following Claude's explicit
instruction in `W/coord/DEKUI_COORD.md` to stop and ask about uncertain sites.
`W` is `I:/_triaevum_work/mm3d-port`. No live acceptance is claimed.

## Implemented slices

The settings foundation loads the tip schema, all 68 added shipped keys and
32 changed defaults. The 588-key donor JSON and embedded fallback are identical
bytes. Save emits 601 keys, including the donor's additional runtime defaults.
The existing size/version checks, typed fallbacks, save blocking, Unicode paths,
atomic replacement and startup/ordinary Apply distinction remain. New tests
check every shipped key, the independent mask/banner migrations, bounds and
the audio version/mode matrix. Renderer preferences are configuration records
in `presentation_options.h`, with no renderer implementation.

Native behavior added: weaker enhanced defense; regular Tatl reminder
suppression; ordinary Bomber approach suppression; owl-intro native hooks;
low-health beep suppression; NPC/enemy, selected prop and vegetation draw
distances; configurable scripted-camera FOV. The full translated defense body,
all explicit Tatl/Bomber/owl guards, and all stock draw windows, pools and
threshold literals remain required. Owl intro's moved bit-table literal is
`0x626c5c`, supported by the native literal proof in the inventory.

There are 150 Core candidates, including 28 new native PCs. The machine-readable
`W/analysis/dekui/update_boundaries_ported.txt` contains every added PC and PC+4,
plus three non-fallthrough targets (58 unique boundaries after deduplication).
It does not claim generated-registry membership. Adapter/CMake/registry files
were not edited. No production CMake change is needed for these header-only
modules; the new replay test compiles through the standalone runner.

The owl-intro native handlers are available and tested, but applying
`sword_activate_owls` keeps normal introductions until OwlSword and its verified
Restoration strike-trigger capability are ported together. This avoids enabling
half of that option. Direct handler tests cover both states.

## Foundation fields whose behavior is still pending

The new song panel/banner, file-select header/ease, independent mask poses,
Gear photo, HUD opacity/banner/D-pad geometry fields roundtrip; their shared UI
draw/input behavior is still pending. Price multiplier 3, six-hour death,
FastTime, StickControls, ClassicDoubleTime, ClassicSaving and OwlSword also
remain pending. Their schema entries do not supply missing native or host
services. Existing price startup behavior is retained until the price slice.
No new save sidecar, text table, fabricated appended code, scratch address or
capability flag was installed. Restoration sources are unchanged in this slice.

`SettingsHost::hle_audio_accuracy` exposes mode 1 (Balanced) and 2 (Accurate HLE).
Every pre-migration preference becomes 1; only marker 1 plus integral mode 1/2
preserves a subsequent choice. The compatibility boolean derives from mode 2.
The host audio consumer remains an integration task.

## Stop/request

PromptText's new upstream archive-pointer address `0x6b373c` has no direct native
literal xref in the supplied reference image. `0x6b3744` is a data-range inference,
not an established accessor proof. The changed NotebookPrompt/FileSelectPrompt
and other localized UI builders depend on this read. Neither address has been
installed in the port. The coordination log requests a proven containing
accessor/xref, or a concrete source-owned language-publication contract derived
from the registered message getter, before this shared UI slice continues.

Remaining work resumes at inventory slice 3; MenuBackdrop, time/pad/source
capabilities, localized messages/prices/classic time and transactional classic
saving follow it. The inventory remains the complete remaining-work plan.

## Renderer and frontend handoff

Class (b) implementations remain with the title renderer: 60 Hz interpolation
for camera, player, actors, effects, particles, water and UI; AO, depth of field,
fog, bloom, widescreen/shrink composition, visibility-depth cache and HD photo
capture. None of their eight optional native sites was added. There are no
required new guest observations for the draw-distance hooks or scripted FOV;
they already modify native register values. Optional later renderer contracts
are timestamped camera/scene identity, draw/layout owner identities,
PauseCapture/PhotoCapture backend events and ClassicSaving rendered-black
acknowledgment. Keep those observations separate from gameplay writes.

Class (c) work remains frontend/configuration: audio mode UI, startup hint,
controller shortcuts, explicit layout reset, translation import/language UI,
versioned resource migration/backup and setup paths. New preference records
roundtrip here; no Qt resource tooling or frontend was imported. The complete
per-file class (b)/(c) ledger is in `W/analysis/dekui/UPDATE_6e36dfb4.md`.

## Standalone validation

`I:/Python/python.exe -B W/analysis/dekui/test_update_port.py settings|core|update`
runs one component; append `--clang-cl` for the application's clang-cl 22.1.6.
Each invocation acquires the shared exclusive mkdir build lock and releases
only its own lock. Outputs stay in `W/analysis/dekui/settings_build_update`;
receipts are beside the runner. No CMake, main build or game is invoked.

The core tests validate the original EUR image and corrupt/remove every word
read by each eligible full guard. The separate replay fixtures check complete
ARM/VFP/CPSR/SP/LR effects, conditional defense, signed Bomber loads, excluded
actions/categories, reminder alarms, owl native return and all draw-load
descriptors. Settings tests retain persistence failure and full roundtrip checks.
The analysis `port_update_*.py` scripts record staged construction, not a single
final-tree regeneration command; do not rerun them over subsequent manual edits.
