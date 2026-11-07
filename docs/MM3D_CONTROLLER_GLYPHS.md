# MM3D native controller glyphs

MM3D uses **mm3d-dekui + Project Restoration**, not OoT TopScreen owners.
The implementation uses the existing black/white glyph atlas and input/configuration
owners. No game art, colored glyph atlas, shader or environment-profile changes.

**Experimental / incomplete qualification — not yet merge-ready.** Sustained HUD,
notebook badge, inventory text, dialogue advance, scripted device switching and
settings now have scoped live evidence. Physical devices, Android/native-HUD touch
integration and exhaustive forms/fades/aspects must not be inferred from those
successes. Current-code OoT identity and a developer-OFF MM smoke pass; one
environment regression suite fails. Inline chord readability and mapped inline
controls inside NPC speech (distinct from the advance indicator) remain gaps.

## Controls

F1 → Controls → Devices, or Esc → Controls:

- **Button prompts** selects Automatic or an explicit family, using the existing
  last-active-device tracking.
- **Native button labels** selects Controller or Original 3DS.
- Glyphs resolve current per-action bindings; disabled mouse bindings are excluded.
  Touch, Original and unbound actions retain native art.
- `oot3d_controls.json` stores `native_button_labels`; absence means enabled.
  Only `false` is serialized for legacy-compatible defaults. Presets preserve it.

Restoration I/II mean **ZR+X / ZR+Y**, not OoT's direct triggers.

## Native ownership

`titles/mm3d/adapter/mm3d_button_overlay.h` observes guarded EUR v1.1 native
widget emission at `0x00134de4`, after visibility/alpha/pass checks and dekui's
temporary transforms. This includes attached Pictograms outside layout trees.
Recognized pure glyph geometry follows the native null-geometry path without
suppressing children. Mixed A/B ring art remains native, with the host key over
its letter. No guest-memory or input-hit-target writes are made by replacement.

Recognized controls include:

- X/Y/I/II HUD and inventory panes;
- A/B ring letter crops and registry-owned `parts` control cells;
- the attached notebook portrait badge and native menu legends;
- four independently bound D-pad directions;
- `MessageWindow` advance and exact `ZR +` font legends;
- individual inline control records emitted by the native text renderer.

Live axis-aligned UV transforms select animated cells. Restoration's amber parts
row maps I/X/Y/II separately; resolved cells take precedence over template pane
names. The notebook fallback validates its Pictogram owner and external anchor.

The inline hook at `0x00144c74` verifies thirteen code words, exact main/shadow
callers and live parts pixel identity. It replaces only known control records,
not a whole string. Ordinary prose, unsupported icons and unknown sources take
the original renderer. Replaced records return the unchanged native command
count; SP/LR, other registers, flags, VFP and guest bytes remain intact. Native
shadow copies do not produce duplicate host glyphs.

Coordinates use native affine matrices and MM's centered 400×240-to-16:9 canvas
conversion. Inline chords fit their reserved text cells. HUD/hotbar chords retain
native key height, anchor the main key over mixed-art letters, and clamp the whole
chord at canvas edges. General localized-text readability remains unqualified.

## Submission and presentation ownership

Captures carry the native command cursor (`0x006a3a88`). GSP submission consumes
only captures in that command list's byte range; other buffers remain pending.
Successful draw enqueue binds immutable snapshots to exact draw IDs. Presentation
combines only snapshots referenced by the completed frame, once per submitted
list. There is **no previous-host-refresh guess or last-nonempty fallback**.

Snapshots preserve captured bindings/families, so a settings/device switch cannot
prematurely remove replacement art whose native geometry was already suppressed.
Empty submissions, address reuse, split lists, delayed presentation and binding
changes have regression tests. Pending/history storage is bounded and resettable;
submitted snapshots discard source addresses. OoT's title seam defaults to no-op.

## Scoped live evidence

The external `analysis/parity/OVERLAY/closure/` plans, immutable attempt markers,
receipts, identities, PNGs and retained BMP hashes define the exact qualifications:

- `submission-hud-cli`: 6000-frame cold HUD/F1-close/reopen route. All fifty
  diagnostic samples from1000 onward have28 primitives, with no intervening
  change-triggered zero gaps; reviewed early and late screenshots retain glyphs.
- `animated-prompts` / `inline-control-records`: notebook portrait Space badge,
  Items/Masks, and X/Y/I/II replacements inside native inventory prose. The
  earlier partial/failing cases remain failures, not retroactive passes.
- `dialogue-advance`: genuine owl dialogue shows Space advance, hides gameplay
  HUD, and restores it on return. Unsupported HOME art is not assigned a fictional
  host binding. This does not cover every NPC/form/inline dialogue message.
- `focused-gameplay-devices`: automatic Xbox/keyboard/PlayStation/Nintendo,
  detach and reconnect; normal guest button delivery after releasing boot-only
  timeline input. Virtual devices and OS-injected keys are **not hardware proof**.
- `touch-owner-boundary`: the actual platform `VirtualPad` API selects Touch,
  restores native art, blocks gameplay input during F1, cancels pending presses,
  and returns to keyboard glyphs. This is a synthetic producer, **not Android
  hit-testing, touchscreen hardware or native-HUD anchor integration**.
- `settled-settings-f1`: actual Devices widget preview, revert, save and reload;
  only the label preference changes. Cold saved choices and the shared Esc owner
  are exercised by `settings-esc-events` and subsequent persistence cases.
- `settings-retry-modal`: a real Windows file-sharing lock produces the actual
  save-error/Retry dialog while preserving file/live state. After unlock, Retry
  saves the choice and gameplay resumes with Original artwork.

The failure test exposed a shared menu bug: an idle desktop pointer swallowed
modal keyboard/controller input. A failing regression reproduces it; the fix
consumes only active pointer presses/releases while preserving modal protection.
Headless real F1-widget tests additionally cover failed save/revert and fresh-owner
reload. `f1-save-error-visible` exercises actual F1 save failure and recovery;
its live preview survives the failed save and the saved file changes only after
unlock. F1 placement now keeps its status footer inside the viewport. The hovered
Save-path tooltip partly covers the error text in the failure screenshot; this
is not a clean full-message readability capture.

## Tests and boundaries

`mm3d_button_overlay_tests` covers resource/UV ownership, code guards, visibility,
finite poses, no guest writes, chords, inline records, binding families, settings,
command ranges and completed-draw history. Pass the owner's EUR v1.1 `code.bin`
to check real guards. Adapter tests verify the inline hook ABI and corrupt guards;
submission-queue tests verify observer ordering. The targeted real-widget suite is
`triaevum_f1_settings_smoke --controls-persistence`.

The generated registry must contain `0x00134de4`, `0x00134de8` and the inline
renderer function entry `0x00144c74`. Startup rejects missing prerequisites.
Use `oot3d_mm3d_single_screen_tests --block-boundaries` and
`titles/mm3d/a32/guest_patch_entries.py` for private regeneration; do not substitute
an older shared registry. The inline renderer entry already existed in the
private registry used by this campaign.

At implementation `0fdf5f18`, thirteen current-source regression suites were run:
twelve pass (including159 overlay checks and real F1 persistence); the environment
suite fails `group night live reload`. A filesystem-timestamp collision is only
a hypothesis, not a demonstrated cause. No unrelated profile/lighting fix or blind
rerun was made; the failed gate remains open.

`current-oot-o2` runs the current public OoT build for400 presentation frames:
all100/200/300 BMPs exactly match `retuned/cap-p13.sha256`. `current-mm-public`
cold-boots2800 frames with developer tools OFF and reviews HUD, notebook badge,
Items inline controls, Masks and return. The developer driver emits no receipts.
This MM build uses embedded blocks, not a certified installed whole-AOT plugin
package. The inline I/II chords are visibly small in the public Items capture;
substitution/native prose preservation is qualified, general readability is not.

Historical failed runs remain failures and older successes retain their original
source identities. Consult external `OVERLAY/RESULTS.md` for exact gate identities
and remaining gaps.

The developer-only acceptance driver never forces prompt preferences, guest state
or widget return values. SDL/OS/platform-boundary actions remain synthetic.
Native OS injection refuses other foreground windows; observed focus failures
are retained. Pointer movement settles before clicking overlap-enabled ImGui tabs.
