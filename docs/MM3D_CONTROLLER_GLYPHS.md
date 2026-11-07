# MM3D native controller glyphs

MM3D uses **mm3d-dekui + Project Restoration**, not the OoT TopScreen
owner table. The title session observes guarded native widget emission at
EUR v1.1 `0x00134de4`, after native visibility/alpha/pass checks and dekui's
temporary transforms. This includes attached Pictograms outside layout trees.
It copies bounds/color/alpha without guest-memory or input-hit-target writes.
Recognized pure glyph geometry takes the native null-geometry path; children
still emit. Mixed A/B ring art remains native.

**Experimental / NOT DONE:** the continuation cold run initially shows the
replacement HUD, but later F1-open frames lose it with unchanged settings.
Do not treat this implementation as merge-ready or complete parity.

### Closure campaign: still blocked

The `alpha-hud`, `context-hud` and `pass-live` attempts did not close the HUD
blocker. Finite alpha saturation and layout-local pass scoping have regression
coverage, but neither is a demonstrated fix for the live loss. In `pass-live`,
frame 5500 lacks the replacement glyphs and frame 5600 has them again. All 58
settings receipts retain Keyboard, enabled native labels and identical controls.
The late diagnostic samples alternate between 0 and 28 primitives; this is
activity evidence, not a root-cause proof or acceptance pass.

Capture accounting now distinguishes unbound bindings, unexpected ring geometry,
capacity limits and duplicate panes, and reports pending label/pane counts.
Those counters were added **after** these runs; they cannot explain earlier
samples retroactively. The overlay suite passes 100 checks with the EUR image,
and the single-screen suite passes. No additional game was launched for the
accounting change. Notebook/pause/dialogue, gameplay device switching, platform
touch, settings persistence and the current-code OoT regression gate remain open.

## Controls

F1 → Controls → Devices, or Esc → Controls:

- **Button prompts** selects Automatic or an explicit family. Automatic shares
  the existing last-active-device tracking with the application menu.
- **Native button labels** selects Controller or Original 3DS.
- Controller labels resolve the current per-action binding. Disabled mouse
  bindings are excluded. Touch/Original/unbound actions retain native art.
- The setting is saved in `oot3d_controls.json` as `native_button_labels`.
  Absent means enabled; only `false` is serialized, preserving default legacy
  documents. Changing a control preset preserves this presentation preference.

The existing black/white 32/64/128-cell glyph atlas provides keyboard symbols,
mouse buttons and Xbox/PlayStation/Nintendo/Steam Deck/generic pad art. No new
colored button-letter atlas or game assets are shipped.

## Ownership and timing

`titles/mm3d/adapter/mm3d_button_overlay.h` recognizes:

- named X/Y/I/II panes in `HudItemIconSet` / `ItemIconSet2`;
- A/B letter crops on their native action-button rings;
- A/B/X/Y/L/R/START crops on the registry-owned `parts` resource, including
  native Pictogram panes and dekui-generated menu legends;
- four individually bound directions within `HudDpad.pane_dpad`;
- the visible `MessageWindow` advance indicator;
- the exact native font records for dekui's `ZR +` menu legend.

Restoration's I/II inputs are **ZR+X / ZR+Y**, not OoT's direct triggers.
Unknown atlas crops, ordinary text, unsupported pointers/code, hidden panes,
invalid transforms and lower-screen passes do not produce replacements.
The observer consumes individual emitted panes, with bounded layout context,
deduplication and a label cap. Standalone emissions without a LayoutBase pass
use the native renderer's own visibility/pass validation. Black shadow copies
are not independent host labels. Coordinates come from final native affine matrices,
including the configured dekui poses; the only canvas conversion is MM's
centered 400×240 origin into its 16:9 presentation canvas.

The host stores complete snapshots, including empty ones, keyed by submitted
PICA draw IDs. Presentation currently selects the previous host-refresh record.
That timing assumption is **not qualified for MM3D**: the closure campaign still
observes alternating empty/nonempty records and visible replacement loss.
Do not retain the last nonempty record as a workaround: genuinely hidden labels
must disappear. Native emission, command-list submission and completed-frame
ownership need to be distinguished before changing this policy.
Reset and backwards IDs clear history.
Snapshots also retain binding/family settings, so a switch cannot remove a
replacement before its corresponding native frame changes. Slots contain
values, not guest pointers to temporary scratch data. Wide labels fit their
native cells, and recognized `ZR +` legends retain their plus.
The title-neutral default methods are no-ops for OoT; its existing overlay is
unchanged. Rendering uses the existing UI texture provider and glyph renderer.

## Validation / qualification boundary

`mm3d_button_overlay_tests` covers native ownership, code guards, visibility,
finite poses, no guest writes, Restoration chords and font legends, all prompt
families, remapping, Original/Touch, persistence and frame-history clearing.
Pass the owner's EUR v1.1 `code.bin` as an optional argument to verify the
ten native emission/null-geometry guard words against the real image.
The generated A32 registry must include BOTH `0x00134de4` and `0x00134de8`.
Regenerate using `oot3d_mm3d_single_screen_tests --block-boundaries` and
`titles/mm3d/a32/guest_patch_entries.py`; the title preflight rejects an old
registry missing either entry.

Pure native geometry suppression and mixed-ring overpaint are different paths.
Their exhaustive live fidelity is not qualified.
Synthetic tests do **not** establish complete live dialogue, every menu,
fade/transition or hardware hot-plug coverage. The lane's external
`analysis/parity/OVERLAY/RESULTS.md` records actual runs and any remaining
qualification gaps; do not infer full parity from this implementation inventory.

### Initial live qualification

The two cold boots at `db04ccff` show native gameplay HUD and generated notebook
legends in keyboard and PlayStation families. **Full parity is not qualified.**
The small A badge under the notebook portrait remains native. Its Pictogram is
bound to an external `disp_a_icon_n` anchor; scanning the final layout tree does
not establish coverage of every separately emitted/attached Pictogram.
The capture route entered Notebook, not inventory pause, and did not exercise
inline dialogue. Those paths need further guarded emission ownership work and
explicit acceptance captures. Hardware hot-plug, live F1 interaction and all
transition/occlusion cases also remain unqualified.

The earlier own OoT-only build at `db04ccff` passed the deterministic O2 gate:
all three BMP hashes matched `cap-p13`. That result does not qualify later code.
No shader or environment-profile sources were changed.

### F04 continuation

- 76 overlay checks, ten shared/title suites and the single-screen adapter suite
  pass. Actual EUR guards and missing/corrupt guard cases are included.
- The coverage attempt faulted at the missing `0x00134de8` continuation before
  capturing notebook/inventory/dialogue. A private regenerated registry fixes
  startup; the failed acceptance case was not rerun.
- A separate device run records live virtual-Xbox/keyboard prompt transitions,
  but remained in the intro. SDL finger injection did not reach the platform
  touch-control API. Neither result qualifies gameplay touch/hardware coverage.
- The widget run cold-boots gameplay and opens F1. Controls/save/reload/restart
  interactions were not established. Later frames lose replacement HUD glyphs
  despite the receipt retaining Keyboard and enabled native labels.
- Notebook badge, pause/dialogue, gameplay switching, touch, persistence,
  transition/occlusion and current-tip O2 acceptance remain open.

The developer-only `TRIAEVUM_OVERLAY_ACCEPTANCE` driver produces input and
observation receipts. Injected SDL/OS events are not physical-device evidence;
it never sets a prompt family or a widget result. See the external
`OVERLAY/continuation/` evidence and `OVERLAY/RESULTS.md` for exact run boundaries.
