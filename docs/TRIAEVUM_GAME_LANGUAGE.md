# Game language selection

2026-09-10. Native game localization, not translation of Forge/F1 itself.

## User behavior

- Forge discovers languages after ROM extraction/adaptation. Its **Game
  language** dropdown becomes available when preparation completes, before
  Launch game. Subsequent Forge sessions reuse the same preference.
- **F1 > Game > Game language** exposes the same languages and saves changes.
- Changes require a full restart. Native resource loaders initialize menu
  textures and messages together; changing just the message-language global in
  a running session is not supported. Loading a save state restores its old
  initialized language and is not a full restart. Ordinary saves are unchanged.
- Preferences live in `config/game_language.json`, next to `TriAevum.json`.
  A compatible preference survives preparation again. A different ROM without
  that language falls back to its first supported language (English for the
  supported EUR/USA inputs). Invalid files are not silently discarded.

## Ownership and evidence

- `oot3d_game_language.{h,cpp}` is a title-adapter module, not renderer logic.
  It reads bounded RomFS directory/file tables and QM v4 message slots. A
  language needs populated message data plus HUD and file-selection resources.
  Neither a filename nor a region label determines availability.
- Slot ordering follows `oot3d_ui/ui_frontend_resources.cpp` and the QM
  resource adaptation in `oot3d_region_assets.py`. CFG system-language IDs
  differ from resource slots. USA resource normalization preserves English,
  French and Spanish; it does not create German or Italian translations.
- `TriAevum --game-language-info ROMFS` exposes the same detector to Forge.
  It reads metadata/message tables only, not all texture payloads, and does
  not initialize the game, title plugin, renderer or audio.
- At boot, `oot3d_native_a32_window.cpp` detects actual available languages
  (including older installations), loads the preference and supplies its CFG
  ID through `NativeA32CtrHostConfig.SystemLanguage`. Original title code then
  selects its own localized resources. No AOT/title rewrite or ROM patch.
- `oot3d_game_language_panel.cpp` implements a registered application tab in
  F1. Atomic config persistence uses the existing host helper. Renderer
  settings, TopScreen settings and save formats are not modified.
- `tools/triaevum_release/game_language.py` handles Forge discovery and
  persistence. Both interfaces share a versioned JSON contract; runtime always
  redetects availability from its actual data rather than trusting that list.

## Verification

- Native detector: real EUR RomFS reports en/de/fr/es/it. Synthetic native
  tables cover EUR, USA, normalized USA, absent menu resources, invalid QM
  extents and truncated images. Set `TRIAEVUM_TEST_RUNTIME` to the built
  executable and run `test_game_language.py` to include native cases.
- Python tests cover persistence, unsupported selection, changing ROM,
  preservation of malformed preferences and the actual Tk combo event.
  Twelve Python tests pass with the native detector enabled. Source and frozen
  Windows Forge GUI probes pass, including combo layout bounds. The one-file
  Forge executable was rebuilt under `J:/TriAevum-verify-20260910/forge-language`.
- Actual F1 widget smoke selects Italiano, checks persistence, restart notice,
  CFG ID 4 on next boot and rejection of unavailable languages (3,819 assertions
  across the complete widget smoke suite).
- Windows/NRI cold boot with `--game-language it` in `probe_renderer.py`
  reached the file-selection menu in Italian. Framebuffer evidence:
  `J:/TriAevum-verify-20260910/language-italian-menu/framebuffer_000840.bmp`.
  The isolated probe exits successfully after 900 frames; no user save changed.
- Native CFG service test already checks block `0x000A0002` returning the
  configured Italian language ID. No platform-specific language logic added;
  Linux/Android device execution is not part of this verification.
