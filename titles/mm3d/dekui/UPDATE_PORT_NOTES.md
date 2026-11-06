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
  game's own German/Italian terms (Inventar, Karte/Mappa, Zeitplan/programma,
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

## S2: shared UI at tip 4f2692ed (2026-11-06)

Method: each module is a three-way merge (`git merge-file`) of our port onto the upstream tip,
with the upstream version our port was made from as the base (beta.1 source or 6e36dfb4) and
both upstream files normalised the way our port names things (namespace `Mm3dDekui`, local
includes). Our translated v1.1 PCs and guard words therefore stay in place; every hex literal
the merge introduced was listed and checked (W/tools/dekui/s2_merge.py, s2_resolve.py).

Shared UI modules now at tip (6e36dfb4 slice 3 plus the tip delta): banners, dialogue_scale,
hud_buttons, inventory_shadow, item_preview, menu_page_switch, menu_tabs, native_file_select,
native_inventory, native_item_present, native_notebook, notebook_rows, notebook_scrollbar,
ocarina_prompt (group transform, button_v, width_label, icon_scale, dungeon_alignment, 32-glyph
flat rows), schedule_rows (TextNew badge repair), song_list, and the new song_header and
song_panel. notebook_prompt, file_select_prompt and items_map_prompt were already at tip.
No new native PC: these modules reuse the existing sites.

Deliberate differences from tip, until their owning slice:
- native_inventory: the HD Gear photo (PhotoCapture) arrives with MenuBackdrop in S4; a local
  inert `PhotoCapture` (enabled false, no layout) keeps the Gear photo layout and border off.
  Its data reads 0x656B0C / 0x63A270 are only reached through that path and are unproved.
- native_item_present: `ItemSlideInterpolation::Offset` is the renderer's (class b); the same
  six-frame linear offset is computed inline.
- hud_buttons / player_hud: `StickControls::ItemLayoutIndex` (S5) is the identity while stick
  controls are off, so the slot index is used directly.
- song_list: `OcarinaInput::Observe` (S5 host input filter) is not called yet.
- ocarina_prompt: English arrow labels keep the native 12-pixel size; only translated labels
  shrink to the 36-pixel slot (S1 decision, kept over the tip's unconditional fit).
- Not ported in S2: menu_help (tip conflicts and a new PC 0x5A58F4), world_map_prompt (its tip
  version needs DungeonMap's tip backgrounds, S3), file_select_input (host input, class c).

Tip HUD (second S2 commit):
- Settings: 588 -> 608 shipped keys (defaults/README.md) plus 10 parser-only keys
  (`dungeon_map_buttons_*`, `dungeon_map_{zoom,move}_prompt_*`). A profile without
  `player_hud_layout_version` stays version 0: no base scale, no padding, magic width 1,
  mirror off, its own minimap opacity and controls. Missing dungeon prompt keys fall back to
  the Notebook values. The dungeon-map arrow/prompt fields are parsed and saved in
  `DungeonMap::Options` now and consumed in S3. `stick_click_controls` keeps a false Load
  default; device routing belongs to our Input owner (S5).
- hud_adjustments and player_hud at tip: base scale, left/right padding (+-100) and
  top/bottom (+-40), magic width before MagicBorder clones the frame, one rupee/key pivot,
  layout versions 0/1/2 with FreshDefaults / VersionOneDefaults, HUD opacity.
- hud_item_background.h (new): mirrored X/Y/I/II backgrounds, last Root of the layout-draw
  Root case and first End of its End case (upstream places it after DoubleTime's ShadeRoot,
  which this port does not have). It needs no PC; the panes come from the Restoration HUD
  layout the RomFS layer mounts. Its 0x1200-byte scratch sits under the same layout-draw SP
  as OcarinaPrompt's 0x2400-byte scratch, which already runs there.
  Proofs (S2): the layer's `Joker.Main.New.gar` (setup fingerprint e08af793...) holds `Hud2`
  (`all`/`item_button_g`/`item_icon_set`), `HudItemIconSet` (`all`, the four `icon_item_*_g`
  groups, each with its `icon_*_base`) and `ItemIconBase` (one pane `bb00_ybtn`); the vanilla
  RomFS has no such archive (work-tree checker `tools/dekui/prove_hud_item_panes.py`). Stack:
  the developer probe `TRIAEVUM_MM3D_SP_PROBE=<file>` (adapter `mm3d_layout_stack_probe.h`)
  logs, at the Root/End PCs 0x1262C8/0x1262D4, SP before/after the Root chain, the mapped stack
  range and the native draw's depth below the lowered SP (pattern fill, untouched words
  restored). Cold boot to gameplay: every layout is drawn on the main thread (TLS 0x1FF82000,
  stack 0x0FFC0000-0x10000000, exheader 0x40000); `Hud2` committed 348/348 times, SP at Root
  0x0FFFF448, largest chain 0x3600 (MagicBorder 0x2400 + HudItemBackground 0x1200), deepest
  native draw 0x428 bytes, least headroom left 0x3BA20 bytes. A savestate restores no
  Restoration session, so `Hud2` (and these buttons) only appear after a cold boot.
- native_minimap at tip: the pending-quad discard for removed radar objects (stale icons in
  the next full map draw), the suppress argument (always false until MenuBackdrop, S4), side
  padding, HUD opacity, and the small-map overview marker rebuild from 6e36dfb4. Vanilla
  v1.1 proof for the fields, now guard words: 0x5E8C24 `ldr r0,[r5,#0x2c8]` and 0x5E8F84
  `str r6,[r5,#0x2c8]` in the radar prepare/draw (upstream's "retail 5E8F84"), 0x5F0C48
  `str r1,[r0,#0x2c4]` / 0x5F0C4C `ldr r6,[r0,#0x2c8]` in the reserve routine.
- Owner decisions (2026-11-06): no dekui FOV, widescreen or draw-distance control is added;
  the renderer FOV multiplier, the window host and one renderer draw-distance setting own
  those, and dekui's values stay internal bases (cutscene FOV 49).
- Tests: `hud_tests.cpp` (`oot3d_mm3d_dekui_hud_tests`, standalone target `hud`), adapted from
  tip player_hud_check/native_inventory_check: mirrored halves, all-or-nothing commit,
  byte-for-byte End, nested and invalid inputs; pending quads under the hide option and the
  transient suppression, no partial discard; small-map overview and restore. Settings tests
  add the layout-version matrix, bounds, the dungeon prompt fallback and the parser-only keys.

## S3: dungeon map at tip 4f2692ed (2026-11-06)

Modules at tip: `dungeon_map.h`, `world_map_prompt.h` (deferred from S2: it needs the tip
DungeonMap backgrounds) and the new `message_layout.h` (the read-only description-plate reader;
the `Panel` value type is declared there because `message_interpolation.h` is the renderer's).
`dungeon_map.h` was rebuilt from the normalised tip file with every code address translated
through `v110_address.h`; `world_map_prompt.h` by three-way merge (`W/tools/dekui/s2_merge.py`).

- Sites (vanilla, no Restoration code): 5 = 0x594644 (ref 0x5944D4, relocated BL SetZoom,
  EUR word ebf07902: RememberZoom, then SetZoom 0x1B2A54); 6 = 0x597774 (ref 0x597604, MOVEQ
  r0,#1: EntryZoom, the SVC keeps EQ and the native BLEQ SetZoom follows); 7 = 0x594D98 (ref
  0x594C28, displaced ADD r0,r5,#0x35C: OverviewCursorNext, OverviewCursorEnd 0x594CD8 only in
  mode 3 on the dungeon page). The 14 ZoomGuards and 12 OverviewCursorGuards are the original
  EUR words (relocated BLs re-encoded: 0x597778 0bf06cb5, 0x594CB4 ebf07627, 0x594CDC
  ebf0761d), including the pools 0x1B2A74 = 0x6890F4 and 0x1B2A78 / 0x1B2570 = 0x7781D8.
  New boundaries (registry v11): the six site/PC+4 words plus AddTargets SetZoom and
  OverviewCursorEnd.
- Data: 0x7781D8 (+0x520 mode word, mode = top byte) and 0x727970 (Radar) are identity
  addresses proved by literals (0x1B2A78, 0x1B2570, 0x1AEBDC, 0x36E268 for the state; getter
  literal 0x188348 for Radar). Upstream's 0x36E2A0 literal is 0x36E268 on v1.1; 0x6B36F8+0x2C is
  0x6B3700+0x2C (message manager, as in file_select_prompt).
- Behaviour: arrow scale and per-arrow offsets about each arrow's centre; the red arrow branch
  drawn in the early pass before MessageWindow (layout 2), the rest in the late pass; the border
  inserted first in layout 2 (queue scan now includes screen 2); side shadows split around the
  map description plate (DescriptionBounds, remembered from the DialogueScale After site);
  inner `bb_map_area_g` shadows hidden while zoomed (modes 1/2); A no longer filtered on the
  dungeon page (still filtered on the world map), with the native touch Zoom buttons hidden and
  the dungeon zoom prompt plus "Move map" drawn by `world_map_prompt.h`.
- Marker style (+0x834, byte +0x837): contract guards `MarkerStyleGuards` (getter 0x188338/
  0x188348, MapTransformUpdate 0x1AEB18..0x1AECB4 with pool 0x1AEBDC, the three `bl 0x1882F8`
  calls that give each store its r0 (0x1AEBA8, 0x1AEC00, 0x1AECAC) and the other-mode branches
  0x1AEB40/0x1AEB44, reader 0x59C624, the 0x36E268 literal: 22 words). The write is gated by
  `MarkerStyleConfirmed`, set after the required T5 confirmation of `+0x837 = (mode == 3) ? 0 :
  1` (W/tools/dekui/s3_marker_probe.py): mode 1 -> 1 in four Woodfall Temple states (entrance
  and Map page), mode 3 -> 0 in two (after the A toggle and after re-entry), all decoded offline
  from states taken with the write disabled; the 23 kept states in W/saves/states agree too
  (mode 1 -> 1 in 20, mode 3 -> 0 in the three file-select states). Mode 2 was never observed
  (site 6 turns it into mode 1 on Map entry): its value 1 rests on the guarded static proof
  (0x1AEC00 bl getter, 0x1AEC04 mov r1,#1, 0x1AEC08 strb). The write keeps upstream's 1..3 bound.
- Guest clock: `Core::GlobalTimeUs()` (upstream CoreTiming `GetGlobalTimeUs`, emulated time)
  is the session's `GuestClock`, the CTR system ticks in microseconds
  (`NativeA32CtrHostServices::SystemTimeMicroseconds`). It advances with guest time only; the
  earlier presentation clock (presentation frames x 1/60 s) is gone. Consumers: the dungeon
  "Move map" ZoomFade (150 ms ramp, 500 ms reset), the ItemsMapPrompt bob and fades.
- The donor's `LogInput` diagnostic (upstream: fopen("/tmp/mm3d-map-input.txt") written and
  flushed on every Map-page input frame) goes to `HostLog`: no file, no static FILE.
- Not ported (class b): `InterpolationIdentity` / `dungeon_map_interpolation.h` (map pan
  identity for 60 Hz interpolation), the `EffectCapture::MapShadow` diagnostic.
- Tests: `dungeon_map_tests.cpp` (`oot3d_mm3d_dekui_dungeon_map_tests [code.bin]`, standalone
  target `dungeon`): v110 translation of every address, branch targets, guard mutation against
  the original image, the marker contract, sites 5/6/7 through `Core::CallSvc` (register
  preservation, same game+scene recall vs a new scene or game, mode 2 never remembered, overview
  skip only for mode 3 on the dungeon page, A filter), and the tip dungeon_side_panels_check
  scenarios (side panels, screen-2 routing, early arrows, hidden zoom buttons and inner shadows,
  marker restore on early exits, description exclusion, arrows), plus the `world_map_prompt`
  dungeon branch (label from the mode byte behind the 0x1B2570 literal, Move map chained under
  the zoom prompt's scratch, its guest-time fade, End order, declined frames). core_tests'
  code.bin manifest has 153 sites. The ctr host tests cover the tick-to-microsecond clock.

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

`I:/Python/python.exe -B W/analysis/dekui/test_update_port.py settings|core|update|language|hud|dungeon`
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
