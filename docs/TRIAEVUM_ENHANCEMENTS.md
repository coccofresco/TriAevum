# Enhancements: HD interface pack, mods, your texture pack, custom RomFS folder

UX plan WP-07 (owner decisions 4, 5, 6). Forge's **Enhancements** page shows
each enhancement with three separate states, and applies them from the game
data kept from the ROM:

- **Downloaded / imported**: the stored archive or files, with their version;
- **Your choice**: what the user turned on;
- **Applied**: what the installed game routes (its runtime receipt in
  `forge-state.json` and the RomFS overlay's `import.json`).

Engine: `tools/triaevum_forge/crates/forge-core/src/enhancements.rs`,
`settings_service.rs`; `forge-mods/src/user_textures.rs`,
`custom_romfs.rs`, `overlay.rs`. Window: `forge-desktop/src/pages/enhancements.rs`.

## Apply changes

`apply_enhancements` is the repair in place (`repair.rs`) with the current
choices: the kept ROM data (`D/sources/`) is rehashed, every asset whose
receipt still proves it is reused, the HD interface pack, the enabled
catalogued mods and the custom RomFS folder are prepared again, and
everything is activated in one transaction. The ROM is asked for only when
the kept data is missing or no longer verifies (explanation
`source.needed`). The user's TopScreen layout is kept; when it differs from
the standard profile, Forge asks first (Keep mine is the default, decision 4).
On the desktop, Apply first downloads what the choices need (the HD pack
while it is on, the mod while it is enabled); a failed download keeps the
native assets and says "Not downloaded".

Android: the installer's HD interface pack card shows **Apply now** when the
pack is stored and the installed game does not use it yet (`ui_pack_status`
reports `applied`); it runs the same job through the installation service.

## HD interface pack (OoT3D Vanilla UI/HUD)

The catalogued pack (allowlist and verification unchanged,
`FORGE_SPEC.md` Appendix B). The choice "Use the HD interface pack" is
`hd_interface_pack` in Forge's preferences `D/config/forge.json`
(`triaevum_forge_preferences_v1`, beside `ui_language`): on by default on
the desktop (decision 5). Install, Update, Repair and Apply honour it: while
it is off a stored pack is neither downloaded nor applied.

## Vanilla+ Link remodel

The catalogued RomFS mod, enabled in `D/config/romfs_mods.json` as before.

## Your texture pack

The user's own Azahar/Citra pack lives in the managed load folder
`D/textures/load/<title id>/` (the installed game's program ID; WP-05C). The
runtime draws it above the HD interface pack and the game's own textures
(`azahar_texture_pack.h`). "Load custom textures" is the graphics settings
owner's switch (`Graphics.TexturePacks.Azahar.LoadCustomTextures`): the game
changes it in F1 (Materials > Texture packs) and in the in-game menu
(Graphics > Custom textures); Forge changes it only through the validated
settings service, while no game and no other Forge uses the installation,
and only in a configuration whose graphics schema it knows (9 to 15).

Import (Forge "Import folder..." / "Import .zip...", F1 "Import..."):

- A path component that is a 3DS title id (16 hex digits starting with
  `00040000`) marks a title folder whose contents are the pack:
  `textures/<id>/`, `load/textures/<id>/`, `<id>/`, or the chosen folder
  itself. With several, the installed title's is used, else the import is
  refused with the list; one folder of another title is used (Azahar matches
  textures by content) and the result says so.
- Without a title folder the pack is flat: its common top folder is
  stripped; Forge asks before placing it under the installed title.
- `.png` needs the PNG signature and an `IHDR` of 1 to 16384 pixels per
  side; `.dds`/`.ktx` are copied (not loaded yet); `.json` must be a JSON
  object (at most 16 MiB); other files are skipped. Links, junctions,
  special files and archive entries outside the pack refuse the import.
- At most 200,000 files, 256 MiB per file, 8 GiB in total; at most 32
  folder levels.
- The pack needs a texture the runtime loads: a `tex1_<W>x<H>_<hash>...png`
  or a `.png` that `pack.json` maps.
- It is copied beside the destination (`.import-*`), verified, then swapped
  with the current pack; on any failure the current pack stays untouched.
- After an import Load custom textures is turned on. Forge reports when an
  explicit F1 folder or `OOT3D_AZAHAR_LOAD_DIRECTORY` would still shadow the
  managed folder ("Use the installation's folder" clears the F1 folder).

A pack left in the Azahar emulator's own folder keeps loading while the
managed folder holds no file; Forge offers **Import it** (the original is
never moved or deleted). Once the managed folder holds a pack it wins, and
the Azahar folder is listed on Data & backups as an inactive
`texture_load_legacy` row.

## Custom RomFS folder (unsupported, decision 6)

**Unsupported: may break the game; not covered by support.** For the user's
own experiments; never packaged, redistributed or downloaded by Forge. Off
by default; turning it on shows that warning first.

Format:

- The folder is `D/mods/custom-romfs/` and mirrors RomFS paths:
  `actor/zelda_link_boy_new.zar` replaces `rom:/actor/zelda_link_boy_new.zar`.
  A folder holding a `romfs/` folder uses that as the root.
- Optional manifest `triaevum-custom-romfs.json`:
  `{"format": "triaevum_custom_romfs_v1", "name": "<1 to 80 characters>"}`
  (other members are ignored). Hidden files, `desktop.ini` and `Thumbs.db`
  are ignored.
- The switch is `D/config/custom_romfs.json`:
  `{"format": "triaevum_custom_romfs_settings_v1", "enabled": true|false}`;
  without the file the folder is off; a malformed file is an error.

Validation rejects the whole folder (nothing of it is applied, the game uses
the catalogued mods and its own files) for: a symbolic link, junction, hard
link (Unix) or special file; a name that is not printable ASCII or contains
`\` or `:`, or a path over 255 characters; an empty file; a file over
64 MiB (the overlay's limit); more than 4096 files; more than 1 GiB in
total; more than 16 folder levels; an invalid manifest. Files the installed
RomFS does not have are flagged and left out (the overlay only replaces
files); files a catalogued mod also replaces are reported, and the custom
folder's file wins.

Applying: the files join the same O3RO overlay as the catalogued mods
(`overlay.rs`). The overlay identity gains a `custom_romfs` member
(`[path, bytes, sha256]` per file) only when the folder applies, so the
catalogue-only overlay and its folder stay the Python Forge's; turning the
folder off and applying again gives the byte-identical catalogue-only
overlay. The overlay receipt records `custom_romfs: {unsupported: true,
name, files, paths}`.

Diagnostics: the report and the support bundle carry one line,
"Enhancements: HD interface pack ...; mods ...; custom textures ...; custom
RomFS folder ACTIVE (unsupported), N files: <RomFS paths>" (names and
counts only, never contents).

## Jobs

`enhancements_status`, `apply_enhancements` (`hud_layout?`),
`texture_import` (`source: {path}`, `flat_confirmed?`),
`enhancement_settings` (`hd_interface_pack?`, `mod?: {id, enabled}`,
`custom_textures?`, `use_managed_texture_folder?`, `custom_romfs?`); all
take `package_root?`, `data_root?` and `android?` like `install`.
