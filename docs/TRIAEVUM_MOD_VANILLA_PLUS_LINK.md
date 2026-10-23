# Optional mod: Vanilla+ - Link Remodel And HD Item Models

Forge can apply **Vanilla+ - Link Remodel And HD Item Models** 1.9 by
**HugAMuffin** (<https://gamebanana.com/mods/705890>) to the user's own game
data. It is off by default. TriAevum never distributes the mod.

## The mod

Credits (GameBanana `_aCredits`): Key Authors - HugAMuffin (3D Artists);
Testing - Fado (QA Lead), M-1 (Hardware Testing), seiunsky_1084 (Hardware
Testing); Tool Creation - Jameriquiah (Code Wizard, author of
[CMB-Exporter](https://github.com/Jameriquiah/CMB-Exporter)).

Licence: CC BY-NC-ND 4.0 (<https://creativecommons.org/licenses/by-nc-nd/4.0/>).
The page's permission checklist allows "Download and install this Mod", asks to
be contacted before redistributing it on other sites or distributing modified
versions, and forbids commercial use.

Archive: `link_remodel_vanilla_plus_1_9.rar`, GameBanana file 1827260
(<https://gamebanana.com/dl/1827260>), 6,861,936 bytes, SHA-256
`8b30a15569e5eb680deda6fdd9582929309ad5acd92dcf70fe2bd8effac0398c` (MD5
`d24eb027e36523dfba9af0f6590f2f78`, as GameBanana publishes it). It is a RAR5
archive whose root is the RomFS root; its install note is "unrar the .zar into
\romfs\actor". It replaces 26 native files, each pinned by size and SHA-256 in
`tools/triaevum_release/romfs_mods.py`:

- `actor/zelda_link_boy_new.zar`, `actor/zelda_link_child_new.zar`: adult and
  young Link with all their equipment (one file per age);
- `actor/zelda_keep.zar`, `actor/zelda_link_opening.zar`,
  `actor/zelda_toki_objects.zar`, `actor/zelda_ganon2.zar`, `actor/zelda_st.zar`:
  shared objects and scenes that draw Link's items;
- 18 `actor/zelda_gi_*.zar`: the "get item" models;
- `misc/menu_link.zar`: the pause-menu Link.

All 26 paths exist in the supported EUR RomFS; the USA adaptation keeps
`actor/` and `misc/` unchanged. Both Link models keep the native 25-bone
skeleton, so native animations and SingleScreen's `zelda_link_ext.zar` (Roll
Dash and landing, a separate new file) still apply.

## How it is applied

1. **Setting.** Forge's "Optional mods" section owns it (checkbox, persisted in
   `data/config/romfs_mods.json`). Nothing else changes it; the game has no
   control for it. "Prepare and install" applies a change.
2. **Archive.** "Download mod", or "Prepare and install" with the mod on and not
   yet stored, downloads the archive from GameBanana; when that fails Forge tries
   the approved mirror (see below). "Select .rar..." copies an archive the user
   downloaded from the mod page. Forge keeps a file in `data/downloads/` only
   after size and SHA-256 match `vanilla_plus_link_1_9_archive` in the revision
   recipe. A download failure never stops the installation: the title is
   prepared with the native models.
3. **Members.** Forge extracts exactly the 26 catalogued members with
   libarchive's `bsdtar` (Windows 10 1803+ ships it as `System32\tar.exe`;
   Linux uses `bsdtar` from libarchive) and verifies each one. Without a RAR
   reader, "Select extracted folder..." takes a folder extracted with any tool;
   its files are verified the same way. Verified members live in
   `data/mods/romfs-sources/vanilla_plus_link-1.9/`.
4. **Overlay.** Preparation writes `data/mods/romfs/<key>/romfs_overlay.o3ro`
   (O3RO v1, `oot3d_native_a32_romfs_overlay.h`): the replaced files are
   appended after the RomFS service view, 16-byte aligned, and only their
   file-record payload offset and size are patched. Each patch stores the
   original bytes it replaces; the runtime rejects an overlay that does not match
   the image before the game reads anything. Base bytes never move, nothing is
   converted. `import.json` records the identity, attribution and licence.
5. **Runtime.** The launch profile gains `--romfs-overlay <file>`; Forge's
   receipt pins its SHA-256 and the launch check verifies it. The CTR host's
   RomFS service (`client_session:file:romfs`) composes the view: GetSize
   reports the larger view, reads come from the image, the patches or the
   appended tail. Without the argument nothing changes.

Save states store the RomFS view size: a state saved with the mod on loads only
with the mod on, and the reverse; the error says so. In-game saves are not
affected.

## Licence boundary

- Packages never contain the archive, its members or any overlay: the release
  policy forbids `.rar`, `.zar` (except the two SingleScreen files) and `.o3ro`,
  and lists the archive's and all 26 members' SHA-256 in `forbidden_sha256`.
- The repository stores only metadata: names, sizes, hashes, credits, URLs.
- Extraction and the overlay happen on the user's machine and stay in the
  private data folder. Nothing derived from the mod is redistributed.
- The only redistribution TriAevum may make is a verbatim mirror of the
  original archive, with attribution, free of charge, as a fallback source.
  CC BY-NC-ND 4.0 permits sharing the unmodified work non-commercially with
  attribution; the page's checklist still asks to be contacted first, so the
  mirror waits for the author's reply and the maintainer's approval.

## Mirror (pending approval)

Nothing has been published. Until approval, `VANILLA_PLUS_LINK_MIRROR_APPROVED`
in `romfs_mods.py` is `False` and Forge never contacts the mirror URL.

Proposed location (a dedicated repository keeps third-party licences apart from
TriAevum's GPL releases):

- repository: `coccofresco/triaevum-mod-mirror` (public);
- release tag: `vanilla-plus-link-1.9`;
- asset: `link_remodel_vanilla_plus_1_9.rar`, byte-identical to GameBanana's;
- URL: `https://github.com/coccofresco/triaevum-mod-mirror/releases/download/vanilla-plus-link-1.9/link_remodel_vanilla_plus_1_9.rar`.

Attribution to publish with the asset (repository README and release notes):

> Unmodified copy of `link_remodel_vanilla_plus_1_9.rar`, **Vanilla+ - Link
> Remodel And HD Item Models** 1.9 by **HugAMuffin**, originally published at
> <https://gamebanana.com/mods/705890>. Credits: HugAMuffin (3D Artists); Fado
> (QA Lead); M-1 and seiunsky_1084 (Hardware Testing); Jameriquiah (Code
> Wizard, CMB-Exporter). Licensed under CC BY-NC-ND 4.0
> (<https://creativecommons.org/licenses/by-nc-nd/4.0/>). No changes were made.
> Provided free of charge, only as a fallback for TriAevum Forge when the
> GameBanana download is unavailable; please download it from and support the
> original page. Size 6,861,936 bytes, SHA-256
> `8b30a15569e5eb680deda6fdd9582929309ad5acd92dcf70fe2bd8effac0398c`. The
> game and its assets belong to Nintendo.

Commands, after approval, from a folder holding the GameBanana download and
`MIRROR_NOTES.md` (the attribution above):

```sh
sha256sum link_remodel_vanilla_plus_1_9.rar   # must print 8b30a155...0398c
gh repo create coccofresco/triaevum-mod-mirror --public --add-readme \
  --description "Verbatim mirrors of third-party mods TriAevum Forge can download; each keeps its author's licence"
gh repo clone coccofresco/triaevum-mod-mirror && cp MIRROR_NOTES.md triaevum-mod-mirror/README.md
git -C triaevum-mod-mirror commit -am "Attribution for the Vanilla+ Link Remodel 1.9 mirror" && git -C triaevum-mod-mirror push
gh release create vanilla-plus-link-1.9 link_remodel_vanilla_plus_1_9.rar \
  --repo coccofresco/triaevum-mod-mirror \
  --title "Vanilla+ - Link Remodel And HD Item Models 1.9 (unmodified mirror)" \
  --notes-file MIRROR_NOTES.md
curl -L -o check.rar https://github.com/coccofresco/triaevum-mod-mirror/releases/download/vanilla-plus-link-1.9/link_remodel_vanilla_plus_1_9.rar
sha256sum check.rar                            # same hash
```

Then set `VANILLA_PLUS_LINK_MIRROR_APPROVED = True`, add the mirror to
`LICENSES/Vanilla-Plus-Link-Remodel.md`, and run `test_romfs_mods.py`.

## Verification

- `tools/triaevum_release/test_romfs_mods.py`: catalogue and credits, the
  recipe contract, settings, download fallback, archive and folder
  verification, real `bsdtar` extraction, overlay layout over a synthetic
  Level-3 RomFS, inert-when-off and cached preparation.
- `tools/triaevum_release/test_forge_gui_mods.py`: the checkbox is the
  persisted setting, an enabled mod downloads before preparation, a failed
  download keeps installing, malformed settings are reported.
- `oot3d_native_a32_romfs_overlay_tests`: O3RO validation and composed reads,
  and the CTR host's RomFS GetSize/Read through an overlay.
- On the EUR RomFS the overlay is 16,859,300 bytes (26 patches, 16,857,572
  appended bytes); every replaced file reads back byte-identical through the
  composed Level-3 metadata and the other 1,948 file records are unchanged.

In-game check: turn the mod on in Forge, "Prepare and install", launch, and
confirm the log line `oot3d_native_game: RomFS mods active (26 metadata
patches, 16857572 appended bytes)`; then look at adult Link (bandolier buckles,
hookshot menu model), young Link (Deku Shield straps), an item-get model and
the pause-menu Link. Turn it off and prepare again: the line is gone and the
models are native.

## Risks

- Link-specific TriAevum features read Link's animations and skeleton, not his
  meshes; the skeleton is unchanged. New materials in the remodel can still
  need PICA shader variants the precompiled corpus lacks, compiled on first use.
- The mod adds its own bow reticle; TopScreen's reticle may draw beside it.
- On a RomFS without one of the 26 paths, preparation reports the missing path
  and installs the title with the native files.
