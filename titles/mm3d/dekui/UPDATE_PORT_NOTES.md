# dekui 6e36dfb4 port status, 2026-10-25

This is a partial, tested port. The remaining class (a) work is stopped at the
unproved PromptText language-archive translation, following Claude's explicit
instruction in `W/coord/DEKUI_COORD.md` to stop and ask about uncertain sites.
`W` is the local work directory (not in this repository). No live acceptance is claimed.

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

Resolved 2026-11-06 by slice S1 (next section); the original request is kept below.

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

## S1: message-archive language contract (2026-11-06)

Sources: tgacquin/mm3d-dekui 4f2692ed `src/mm3d/prompt_text.h` (contract part: `Language`,
`Translate`, `Get`, `Codes`, French/Spanish columns) and the language hunks of its
`menu_help.h`, `notebook_prompt.h`, `ocarina_prompt.h`, `items_map_prompt.h`,
`file_select_prompt.h` and `world_map_prompt.h`; zilzo75/dekui-pal daf640f6 as evidence of the
EUR mask semantics only. dekui-pal's `Language()`/`Locale()` integers and its German/Italian
strings are not used.

- `message_archive_language.h` (new): `PromptLanguage` (English, French, Spanish, German,
  Italian), `ArchiveRegion`, `ArchiveLanguage{valid, region, language, raw_mask}`,
  `FromRawMask` (USA 0x2/0x4/0x8, EUR 0x10/0x20/0x40/0x80/0x100; anything else is invalid and
  English), `UsaTableCode` (2/4/8, none for German and Italian) and `ReadArchiveLanguage`.
  The reader checks 22 original EUR v1.1 words of the chain getter 0x1C4FC8 -> both caller
  chains (`mov r4,r0` at 0x13541C and 0x13586C, `add r0,r4,#0x38` at 0x1355D8/0x1359A4) ->
  0x145408 -> reader 0x15A0F8 (`stm` at 0x15A134, "GMSG" pool 0x15A1E8), including the eight
  call-site words that connect it (`bl` getter at 0x11E704/0x11ED40, `bl` 0x13540C at 0x11E710,
  `mov r4,r0` at 0x11ED44, `bl` 0x135868 at 0x11ED48, `bl` 0x145408 at 0x1355DC/0x1359A8, `bl`
  0x15A0F8 at 0x145430), before it reads the archive pointer 0x6B3744, then accepts only "GMSG",
  u16 version 1, u16 0 and a known mask.
  Upstream's raw 0x6B373C is not read.
- `prompt_text.h` (new): consumers receive `PromptLanguage`; `Language(m) == 2` and
  `Translate(2, ...)` do not compile (negative `try_compile` in `host_services.cmake`, and the
  standalone runner). The German and Italian columns are TriAevum's own, written with the
  game's own German/Italian terms (Inventar/Strumenti, Karte/Mappa, Zeitplan/programma,
  Fall/evento, Wecker/Sveglia, Melodien/Melodie, Zoom +/-, Abbrechen/Annulla). Three labels the
  current consumers draw and upstream later renamed (`Song list`, `Zoom In`, `Zoom Out`) carry
  TriAevum French/Spanish too. Unknown labels and empty cells fall back to English.
- Consumers: every prompt label goes through `PromptText::Get` and `Codes` (UTF-8 to code
  points), and template font checks use the translated label's glyphs. The branches that
  differ by language compare with `PromptLanguage::English`: menu help's horizontal row is
  measured and fitted only for a translated archive; notebook labels shrink to stay on screen
  only when translated (4f2692ed's `fit`, scratch 0x3000 and glyph budgets 16/9/18/8); the arrow
  prompt's label fits its 36-pixel slot only when translated; menu help adds the translated
  labels to its font alphabet only when translated. With an English archive (EUR 0x10, USA 0x2)
  or any rejected archive every consumer draws exactly what it drew before S1.
  Live status: only the translated file-select pair prompt has been seen in game (German and
  Italian boots). The translated notebook, menu-help, Items/Map arrow, world-map zoom and
  ocarina paths are verified by fixtures only so far.
- `MenuHelp::EquipLabel(PromptLanguage, bool)` is ported for the StickControls layout (S5).
- Text tables keyed by USA codes (single_screen_text, prices, classic text) are untouched here;
  they move to `UsaTableCode` with their tip versions in S6/S7. Translation packs (S7) key by
  `raw_mask`.
- Tests: `language_tests.cpp` (`oot3d_mm3d_dekui_language_tests [code.bin]`, standalone target
  `language`): the 8 masks, the 16/32 collisions, invalid masks, every header and pointer
  rejection, upstream's 0x0063D33C, each guard word corrupted and removed, the guard words
  against the user's code.bin, every column decoded and within its consumer's glyph budget,
  and consumer fixtures for all masks (pair and arrow prompts, horizontal menu help, both
  notebook pages) that check the drawn glyphs, the English/translated branches and that guest
  memory is restored.

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

`python -B W/analysis/dekui/test_update_port.py settings|core|update|language`
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
