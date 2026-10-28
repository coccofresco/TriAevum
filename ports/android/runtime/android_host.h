#pragma once

#include <string>
#include <string_view>

void InitializeAndroidGameHost();

// The in-game menu's "Export saves" / "Share diagnostics" (InGameMenuHost::
// OpenInstallerPage): records the installer page ("saves" or "diagnostics")
// for the activity, which opens it after the game has quit (the pages refuse
// a save import while the game's process lives). The caller then quits the
// game through its normal exit path. False, with a reason, for another page.
bool RequestAndroidInstallerPage(std::string_view page, std::string* error);

// The Activity left the foreground (TriAevumActivity.onPause through
// NativeHostBridge.requestPause): once, true; the frame loop then opens the
// in-game menu so the game is held when the player returns.
bool TakeAndroidPauseRequest() noexcept;
