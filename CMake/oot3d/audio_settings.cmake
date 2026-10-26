# The host audio output settings (UX plan WP-06C, docs/TRIAEVUM_AUDIO_SETTINGS.md).
# The owner (Ship::AudioOutputPolicy) and the output gain stage live in the
# host audio service (three_ds_recomp_runtime, ship/audio); the game frontend
# contributes F1's Game > Audio section. The menu rows are in oot3d_ui_menu.
target_sources(oot3d_native_game PRIVATE
    tools/oot3d/native_game_runtime/triaevum_audio_settings_panel.cpp)
