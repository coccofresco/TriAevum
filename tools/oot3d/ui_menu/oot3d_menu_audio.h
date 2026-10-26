#pragma once

// The in-game menu's audio rows (Game tab > Audio), apart from any owner:
// the host binds them to its audio output policy (oot3d_in_game_menu.cpp,
// docs/TRIAEVUM_AUDIO_SETTINGS.md). Every change goes through the bound
// callbacks; nothing here keeps a value.

#include "oot3d_menu_model.h"

#include <functional>

namespace Oot3dNativeGame::Menu {

inline constexpr int kAudioVolumeStep = 5;

struct AudioRowBindings {
    std::function<int()> Volume; // percent, 0..100
    std::function<WriteResult(int)> SetVolume;
    std::function<bool()> Muted;
    std::function<WriteResult(bool)> SetMuted;
    // Absent: the "Mute when unfocused" row is left out (Android).
    std::function<bool()> MuteWhenUnfocused;
    std::function<WriteResult(bool)> SetMuteWhenUnfocused;
};

// The Audio heading and its rows, appended to `tab`. Without the volume and
// mute bindings nothing is appended.
void AppendAudioRows(Tab& tab, const AudioRowBindings& bindings);

} // namespace Oot3dNativeGame::Menu
