# Moonfall Curse

Moonfall Curse brings The Legend of Zelda: Majora's Mask 3D to PC on the TriAevum engine, the
engine it shares with Echoes of Time. The mm3d-dekui single-screen interface and Project
Restoration are built in.

**No game assets are included.** You need your own dump of the European Majora's Mask 3D cartridge
(CTR-P-AJRP). It is the only input. The Forge reads it and prepares the game on your computer.

## Install

1. Extract the package into a folder you can write to.
2. Start `TriAevumForge.exe`. Choose your dump (extracted `exheader.bin`, `code.bin` and
   `romfs.bin`, or a supported container). The Forge checks it against the supported revision.
   It then prepares the game, and rebuilds Project Restoration's HD HUD layouts from your dump.
3. Start `Moonfall_Curse.exe`.

The Forge and the game keep your data (settings, saves, logs, caches) in the data folder the Forge
shows. See `docs/MOONFALL_CURSE.md` (section "Playing Moonfall Curse") and the release notes in
`docs/releases/v0.5.0-alpha.1.md`.

## What is in this package

- Our code: the game runtime, the Forge, the update helper and the precompiled title module. Its
  corresponding source is in `source/` (see `docs/SOURCE_OFFER.md`).
- Open-source resources with their licences in `LICENSES/`: mm3d-dekui (`LICENSES/mm3d-dekui.md`),
  Project Restoration (`LICENSES/Project-Restoration.md`), the iMMERSE MXAO module
  (`LICENSES/iMMERSE-MXAO.md`), and the libraries listed in `docs/THIRD_PARTY_NOTICES.md`.
- Nothing from the game: the files derived from your dump are created at install time, only on your
  computer.

## Credits

- mm3d-dekui, by tgacquin and contributors (GPL-2.0): the single-screen interface.
- Project Restoration, by leoetlino and contributors (GPL-2.0-or-later).
- iMMERSE MXAO, by Pascal Gilcher (under its own licence, as in Echoes of Time).

Moonfall Curse is a fan project. It is not affiliated with or endorsed by Nintendo. The
Legend of Zelda and Majora's Mask are trademarks of Nintendo.
