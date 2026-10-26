#include "oot3d_menu_audio.h"

#include <string>

namespace Oot3dNativeGame::Menu {

void AppendAudioRows(Tab& tab, const AudioRowBindings& bindings) {
    if (!bindings.Volume || !bindings.SetVolume || !bindings.Muted || !bindings.SetMuted) return;
    Row heading;
    heading.Key = "Audio";
    heading.Kind = RowKind::Heading;
    tab.Rows.push_back(std::move(heading));

    Row volume;
    volume.Key = "Master volume";
    volume.Kind = RowKind::Number;
    volume.Help = "The volume of everything the game plays. A short tone plays at the new level.";
    volume.Minimum = 0;
    volume.Maximum = 100;
    volume.Step = kAudioVolumeStep;
    volume.Format = [](int value) { return std::to_string(value) + "%"; };
    volume.Read = bindings.Volume;
    volume.Write = bindings.SetVolume;
    const auto muted = bindings.Muted;
    volume.Available = [muted] { return !muted(); };
    volume.UnavailableReason = "Sound is muted. Turn Mute off first.";
    const auto read = bindings.Volume;
    volume.Modified = [read] { return read() != 100; };
    tab.Rows.push_back(std::move(volume));

    Row mute;
    mute.Key = "Mute";
    mute.Kind = RowKind::Toggle;
    mute.Help = "Silences the game. The volume is kept.";
    mute.Read = [muted] { return muted() ? 1 : 0; };
    const auto setMuted = bindings.SetMuted;
    mute.Write = [setMuted](int value) { return setMuted(value != 0); };
    mute.Modified = muted;
    tab.Rows.push_back(std::move(mute));

    if (bindings.MuteWhenUnfocused && bindings.SetMuteWhenUnfocused) {
        Row unfocused;
        unfocused.Key = "Mute when unfocused";
        unfocused.Kind = RowKind::Toggle;
        unfocused.Help = "Silent while another window is active.";
        const auto readUnfocused = bindings.MuteWhenUnfocused;
        unfocused.Read = [readUnfocused] { return readUnfocused() ? 1 : 0; };
        const auto setUnfocused = bindings.SetMuteWhenUnfocused;
        unfocused.Write = [setUnfocused](int value) { return setUnfocused(value != 0); };
        unfocused.Modified = readUnfocused;
        tab.Rows.push_back(std::move(unfocused));
    }
}

} // namespace Oot3dNativeGame::Menu
