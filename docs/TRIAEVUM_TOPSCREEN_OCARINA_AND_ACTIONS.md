# TopScreen: Ocarina And Mapped Actions

Follow-up to the ocarina owner/text and native item-dispatch reports. This
records source evidence and actual Windows NRI/Vulkan gameplay qualification,
not merely successful host planning or configuration parsing.

## Corrections

- **Ocarina guide:** D-pad Up hides/restores the guide; a held press does not
  toggle repeatedly. Short edges survive guest-refresh/native-update gaps.
  The hidden preference survives closing/reopening the instrument; quickload
  resets host-transient state. The original distinct hide/show sounds are
  delivered through the native audio entry at a safe guest-call boundary.
- **Mapped icons:** the HUD previously kept the default eye/ocarina/placeholder
  icons regardless of the configured actions. A separate D-pad presenter now
  follows the child/adult mapping, native inventory and equipped values.
  `None`, unavailable equipment, age restrictions and `RenderDpadIcons=false`
  suppress the respective icons. Face-button items remain independent.
- **Direct child items:** the old consumer wrote the temporary action and
  player trigger, but the slot-3 resolver still returned the ordinary assigned
  item. Host planning succeeded while Link did nothing. The existing compiled
  dispatch now observes the temporary item's lifetime, returns its native
  ItemId, and preserves the mod's enabled/age gates and resolver precedence.
  This does not rewrite inventory slots or replace item gameplay/animations.

## Source Evidence

Official TopScreen 2.1.1, by **rlgcarrot**, from the existing private reference
described in [the port report](OOT3D_TOPSCREEN_2_1_1_PORT.md). This is a
reimplementation, not execution or distribution of its payload.

| Original owner | Recovered behavior |
| --- | --- |
| `005D3D30`, `005E3E6C` | Page-12 Up-edge guide toggle, persistent hidden flag |
| `005D4244/48`, native `0037547C` | Show `01000498`, hide `01000499` sounds |
| `005CB090` | Four configurable directions, separate age tables |
| `005D4A8C`, `005E1840/1830` | Mapped icon centers X=(30,29,9,51), Y=(45,90,67,67) |
| `005D7D14`, `005D7A68`, `005D7B78` | 14-unit icons, equipment groups, direct-item availability |
| `005D8AB8` | Map-toggle atlas glyph; not an inventory ItemId |
| `005CC440` replacing `002C3970` | Temporary direct item takes precedence for slot 3 |
| `00588ECA`, `00506C58`, `0058795C` | Enabled byte, item-age table, native age |

The jump table at `005D8F68` requires **signed halfword offsets multiplied by
four** (`ADD pc, pc, r3, LSL #2`). The existing Ghidra export omitted that
scale in its unresolved indirect-call rendering. Icon cases were checked
against the instruction stream rather than copied from that pseudocode.

Likewise, the guide sound call at `005D4308` has six ABI arguments, although
that Ghidra callsite displays only one: `(sound, 0, 4, 0054AC20, 0054AC20,
0054AC24)`. The last two are passed on an aligned guest stack; the adapter
restores the temporary stack words after invocation. A one-argument attempt
faulted during the qualification run and was rejected, not shipped as a fix.

## Ownership

- `ui_topscreen/oot3d_top_screen_ocarina.*`: browser, guide geometry, toggle
  and native sound event. It does not synthesize note recognition.
- `ui_topscreen/oot3d_top_screen_dpad_presentation.*`: bounded native state
  reader and mapped-icon presenter, using the existing native atlas API.
- `ui_topscreen/oot3d_top_screen_item_guest.*`: original slot-resolution rule.
- `ui_topscreen/oot3d_top_screen_item_dispatch.*`: the same entry registry is
  used by compiled observation and execution; direct-item state is published
  by the gameplay-action owner, not stored in or lost by the HID sampler.
- `native_game_runtime/oot3d_native_ui_lifecycle_bridge.*`: composes these
  primitives and removes the superseded fixed D-pad copies only.

No NRI/PICA shader, whole-AOT generator, title DLL, save format or lower-screen
framebuffer promotion was added or changed. HUD scaling still runs once at
the existing compositor boundary. The changes are shared by platform hosts;
this report does not claim new Android or Linux binary qualification.

## In-Game Results

Private evidence: `J:/TriAevum-verify-20260910/`. Isolated configuration, copies
of the original completion save, fixed native 30 Hz, interpolation disabled,
framebuffer captures rather than Windows screenshots. Runs are bounded and
exit normally. Original save files are not patched or overwritten.

| Test | Observed result |
| --- | --- |
| `ocarina-completion-v2` | Guide visible, hidden by Up, restored by another Up |
| `ocarina-recognized` | X-A-Y-X-A-Y recognized; native animated staff and recognition effect; normal HUD returns |
| `ocarina-final-abi` | Final binary, corrected six-argument sound call: 700 frames, guide toggles, recognized melody, exit 0 |
| `action-iron_boots/run` | Equipment `1122 -> 2122` |
| `action-hover_boots/run` | Equipment `1122 -> 3122` |
| `action-sword_toggle/run` | Equipment `1122 -> 1123`, sword item `3C -> 3D`, actual model changes |
| `action-all_boots_toggle/run` | Equipment `1122 -> 2122` |
| `action-tunic_toggle/run` | Equipment `1122 -> 1222`, actual Goron tunic |
| `action-shield_toggle/run` | Equipment `1122 -> 1132`, actual Mirror Shield |
| `action-tunic-mapped` | One held gesture causes one equipment change/refresh; only mapped tunic icon remains and updates |
| `native-child-fixture` | Native Temple of Time sword interaction changes adult to child; no age/inventory patch |
| `action-{boomerang,slingshot}-child-field` | Before resolver repair: temporary action nonzero, actual player action stays zero |
| `action-{boomerang,slingshot}-child-field-fixed` | Same inputs after repair: actual player action 20/15, boomerang flight and slingshot shot visible |
| `direct-item-lifecycle` | Slingshot ammunition 50 -> 49; ordinary ZR cancels temporary action; slot assignments unchanged |
| `action-{boomerang,slingshot}-adult-denied/run` | Both restricted child items rejected for adult Link; equipment, assignments, ammunition and player actions unchanged; exit 0 |

The child fixture uses the existing native transition helper to enter Temple
of Time at entrance `02CA`, then presses A for the **original** change-age
interaction. The resulting native child state is transitioned to Hyrule Field
for weapon tests. Temple-only attempts are retained as intermediate evidence,
not counted as successful direct-item gameplay tests.

Unit regressions cover all 14 mapping values for both ages, all four direction
identities, atlas bounds, equipment-icon updates, unavailable inventory and
HUD suppression; direct-item dispatch tests cover compiled observation,
native ABI return, no memory mutation, age/enable gates and slot isolation.
Guide tests cover edge accumulation, held/repress, persistence, reset and
distinct single-consumption sound events.

## Reproduction And Limits

Use `tools/triaevum_release/probe_renderer.py` with private native-save-derived
checkpoints, the matching product profile and current runtime. Reusable input
timelines live in `tools/oot3d/native_game_runtime/input_timelines/`:

- `topscreen_ocarina_recognition.json`: adult free gameplay, learned Zelda's
  Lullaby, default Ocarina-on-Down mapping; 700 frames, capture every 20.
- `topscreen_direct_item_lifecycle.json`: child free gameplay with slingshot
  owned and Up mapped to `slingshot`; 280 frames, save at 250.

**Correction to the earlier gap list:** recognition feedback was not absent.
It is produced by the native message/ocarina path and is now observed in play.
Reference notes in the guide are not a history of performed notes. Do not
add a duplicate history or promote native quads 39..44 based on that assumption.
The native submit is `00425930`; the actual ocarina draw is `0042676C`.

Full visual parity for free-play instructions and first-time song-learning
remains unqualified: this fixture already knows all songs, and no matching
new Azahar capture was made. The guide/recognition and action results above
must not be described as every ocarina state or the entire mod being complete.
The additional `gear-practice-entry`, `gear-song-entry`, and `gear-song-confirm`
probes only reached Gear and selected the ocarina icon; they did not enter song
practice and are not counted as practice or first-time learning qualification.
The new mapped presenter also does not claim pixel-identical small decorative
cycle badges or native per-item dimming during every restricted context.

[PR #18](https://github.com/coccofresco/TriAevum/pull/18), by **999sian**, supplied
useful ocarina scope and gameplay evidence as credited in the earlier owner
report. These fixes are independently integrated at the native ownership
boundaries, not a wholesale merge of that PR or a claim of its authorship.
