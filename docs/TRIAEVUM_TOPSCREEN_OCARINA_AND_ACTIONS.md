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
The mapped presenter now includes the original sword/boots cycle marks from
`custom_menu`: payload rectangle `005E1894` is `(458, 2, 40, 40)` in the
512-pixel atlas; the original draw uses 14-pixel marks and direction-specific
sword offsets. Unit tests cover atlas selection, geometry and layering. These
marks still require paired in-game visual qualification; native per-item
dimming now reuses the source HUD opacity for mapped ZR/ZL items and ocarina,
but has not been visually qualified in every restricted context.

The supplied practice saves and their actual qualification results are tracked
in [Practice Save Fixtures](TRIAEVUM_TOPSCREEN_PRACTICE_SAVE_FIXTURES.md).

### Native Item Opacity Follow-Up (2026-09-11)

The original mod does not invent another availability rule for these icons.
In `005D4A8C`, it captures the first vertex alpha from the live native item
renderer (`renderer+0x18`, quad stride 0x40, alpha +0x0C): region 0 goes to
`005E21D0`, region 3 to `005E21CC`, region 4 to `005E21D4`. The mapped draw
then multiplies that value by HUD alpha `005E2204` exactly once.

Do not confuse the texture-category loop with the configured action enum:
the signed-halfword jump table at `005D8F68` sends category 2 to `005D8998`
(action 5, ItemZr), category 3 to `005D8A88` (action 6, ItemZl).
This was checked against ARM instructions, not the incomplete Ghidra switch.

`AppendTopScreenNativeItemIconCopies` now publishes a typed, unscaled opacity
snapshot during its existing read. It collects ocarina opacity even when the
old source D-pad quad is suppressed. The lifecycle bridge passes that snapshot
to the mapped presenter; no extra memory scan, native call, persistent cache,
new gameplay availability policy or renderer-specific code is needed.
Equipment cycle marks retain their original independent HUD alpha.

Unit tests cover distinct source-lane alphas, the hidden ocarina source lane,
zero opacity, single HUD multiplication and unaffected boots. Both the unit
target and Windows runtime build pass. Run the pinned build script through
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File`, not directly inside
PowerShell 7: the latter failed the post-link Authenticode module import in
this environment. The required DLL signature/version check was not bypassed.

`practice-ocarina-opacity` completed 700 native-30-Hz frames with the rebuilt
runtime and the unmodified AD child progress carried through a native scene
transition. Captures show the guide and native note staff. **Correction:** the
upper text box is empty at frame 120 while the generated text is pending, but
frame 160 displays Zelda's Lullaby correctly. A single transitional capture
did not establish a missing title. This remains a regression smoke test, not
proof of complete free-play instructions or first-time learning parity.
No paired Azahar reference was obtained in this tranche.

### Unknown-Song Atlas Repair (2026-09-11)

The partial-progress fixture exposed a different, real omission: selecting
the unlearned Sun's Song suppressed the learned name and reference notes but
also left the unknown marker invisible. The marker rectangle `(406,484,54,24)`
was being sampled from custom_menu, whose corresponding pixels are empty.

Original `005CC694` creates the marker renderer through `005CAA5C` with texture
slot **10**, not a custom texture: globals `005E3F1C` (quad renderer),
`005E3F18` (model) and `005E3F14` (instance) are later consumed by `005D2078`.
`005CAA5C` resolves that slot through native `002E11D0`. The shared contract
identifies slot 10 as **ItemIcons**. The title bridge now passes that live
native identity to the marker presenter. No texture asset, renderer shader,
UV rectangle, save progress or gameplay rule was changed.

The new test asserts the marker's texture identity, source rectangle and
layer, in addition to the existing unlearned-song note/title suppression.
The reusable timeline `topscreen_ocarina_partial_progress.json` requires
free gameplay with Ocarina on Down, song 0 learned and song 1 unlearned:
open instrument, browse learned -> unknown -> learned, then close with B.

Windows Vulkan/NRI run `ocarina-partial-progress-fixed` completed 540 frames
at native 30 Hz. Compared with the same pre-fix timeline, frames 240/260/280/300
differ only inside `(570,89)-(710,148)` at 1280x720: the three tinted question
marks. Frames 160/360 (learned title/notes) and 480/520 (gameplay after B) are
pixel-identical. Unit tests pass. This verifies a real fix without attributing
the already-working generated song title to this change. No new paired
Azahar capture or first-time teaching sequence is claimed.

The cutscene-selector experiment `tomb-native-transition-setup1` is rejected:
applying FFF1 at accepted transition still faults at native 00371788, inside
the aligned-memory-copy routine. This disproves the previous claim that an
early checkpoint write alone explained the fault. The experimental runtime
extension and generator were removed before the final build. Do not retry
the same selector as if its preconditions were established, or modify memcpy
to suppress the fault. First-time learning was still unqualified at this stage.

### Native First-Time Learning Follow-Up (2026-09-11)

The Song of Storms windmill lesson now passes an end-to-end Windows Vulkan/NRI
test with original unlearned practice-save progress: NPC teaching, native
repetition staff, player notes, storm and reward/event updates. The checkpoint
comparison confirms quest bit `00020000` and completion event bit `20` were
set by native gameplay. No production code or reward override was added.
Only the private setup clone's player position was relocated beside the NPC;
this is not coverage of navigating to the windmill. The reproducible timeline,
state verifier, native evidence and precise qualification limits are recorded
in [Practice Save Fixtures](TRIAEVUM_TOPSCREEN_PRACTICE_SAVE_FIXTURES.md).
Other teaching contexts, paired original-mod captures and restricted-action
opacity coverage remain separate work; this result does not close them all.

[PR #18](https://github.com/coccofresco/TriAevum/pull/18), by **999sian**, supplied
useful ocarina scope and gameplay evidence as credited in the earlier owner
report. These fixes are independently integrated at the native ownership
boundaries, not a wholesale merge of that PR or a claim of its authorship.
