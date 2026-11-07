# MM3D native controller glyphs

MM3D uses **mm3d-dekui + Project Restoration**, not the OoT TopScreen
owner table. The title session observes the guarded native layout draw hook
**after** dekui has installed its temporary transforms and scratch prompt trees.
It copies label bounds, color and alpha to host records; it does not change
native text, textures, input hit targets, or guest memory.

## Controls

F1 → Controls → Devices:

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
- the visible `MessageWindow` advance indicator;
- the exact native font records for dekui's `ZR +` menu legend.

Restoration's I/II inputs are **ZR+X / ZR+Y**, not OoT's direct triggers.
Unknown atlas crops, ordinary text, unsupported pointers/code, hidden panes,
invalid transforms and lower-screen passes do not produce replacements.
The walker follows embedded layouts and selected groups with bounded depth,
node count, cycle detection and a 256-label cap. Black shadow copies are not
independent labels. Coordinates come from final native affine matrices,
including the configured dekui poses; the only canvas conversion is MM's
centered 400×240 origin into its 16:9 presentation canvas.

The host stores complete snapshots, including empty ones, keyed by submitted
PICA draw IDs. Presentation selects the previous materialization, matching
native command-list submission timing. Reset and backwards IDs clear history.
Slots contain values, not guest pointers to temporary scratch data.
The title-neutral default methods are no-ops for OoT; its existing overlay is
unchanged. Rendering uses the existing UI texture provider and glyph renderer.

## Validation / qualification boundary

`mm3d_button_overlay_tests` covers native ownership, code guards, visibility,
finite poses, no guest writes, Restoration chords and font legends, all prompt
families, remapping, Original/Touch, persistence and frame-history clearing.
Pass the owner's EUR v1.1 `code.bin` as an optional argument to verify the
four native draw-hook words against the real image.

This is a host overpaint overlay: native mixed ring art remains underneath.
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

The separate own OoT-only build passes the deterministic O2 Kokiri gate: all
three BMP hashes match `cap-p13`. No shader or environment-profile changes were
made for this overlay.
